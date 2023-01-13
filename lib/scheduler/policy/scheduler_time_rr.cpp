/*
 * Copyright 2013-2022 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include "scheduler_time_rr.h"
#include "../support/config_helpers.h"

using namespace srsgnb;

/// @brief Algorithm to select next UE to allocate in a time-domain RR fashion
/// @param ue_db map of "slot_ue"
/// @param rr_count starting index to select next UE
/// @param p callable with signature "bool(slot_ue&)" that returns true if UE allocation was successful
/// @return true if a UE was allocated
template <typename Predicate>
bool round_robin_apply(const ue_list& ue_db, uint32_t rr_count, const Predicate& p)
{
  if (ue_db.empty()) {
    return false;
  }
  auto next_it = ue_db.lower_bound(to_du_ue_index(rr_count % MAX_NOF_DU_UES));
  for (uint32_t count = 0; count < ue_db.size(); ++count, ++next_it) {
    if (next_it == ue_db.end()) {
      // wrap-around
      next_it = ue_db.begin();
    }
    if (p(*next_it)) {
      return true;
    }
  }
  return false;
}

/// Allocate UE PDSCH grant.
static bool alloc_dl_ue(const ue& u, ue_pdsch_allocator& pdsch_alloc, bool is_retx)
{
  if (not is_retx and not u.has_pending_dl_newtx_bytes()) {
    return false;
  }

  // Prioritize PCell over SCells.
  for (unsigned i = 0; i != u.nof_cells(); ++i) {
    const ue_cell& ue_cc = u.get_cell(to_ue_cell_index(i));
    // Search available HARQ.
    const dl_harq_process* h = is_retx ? ue_cc.harqs.find_pending_dl_retx() : ue_cc.harqs.find_empty_dl_harq();
    if (h == nullptr) {
      continue;
    }

    // Search for available symbolxRB resources in different SearchSpaces.
    const cell_configuration& cell_cfg_cmn = ue_cc.cfg().cell_cfg_common;
    for (const search_space_configuration* ss_cfg : ue_cc.cfg().get_search_spaces(ue_cc.active_bwp_id())) {
      span<const pdsch_time_domain_resource_allocation> pdsch_list = ue_cc.cfg().get_pdsch_time_domain_list(ss_cfg->id);

      bwp_configuration bwp_cfg = ue_cc.cfg().dl_bwp_common(ue_cc.active_bwp_id()).generic_params;
      if (ss_cfg->type == search_space_configuration::type_t::common) {
        bwp_cfg = ue_cc.cfg().dl_bwp_common(to_bwp_id(0)).generic_params;
        if (cell_cfg_cmn.dl_cfg_common.init_dl_bwp.pdcch_common.coreset0.has_value()) {
          bwp_cfg.crbs = get_coreset0_crbs(cell_cfg_cmn.dl_cfg_common.init_dl_bwp.pdcch_common);
        }
      }

      for (unsigned time_res = 0; time_res != pdsch_list.size(); ++time_res) {
        const pdsch_time_domain_resource_allocation& pdsch = pdsch_list[time_res];
        const cell_slot_resource_grid&               grid  = pdsch_alloc.dl_resource_grid(ue_cc.cell_index, pdsch.k0);
        prb_bitmap                                   used_crbs = grid.used_crbs(bwp_cfg, pdsch.symbols);
        unsigned nof_req_prbs                                  = is_retx ? h->last_alloc_params().prbs.prbs().length()
                                                                         : ue_cc.required_dl_prbs(time_res, u.pending_dl_newtx_bytes());
        // As per TS 38.214, 5.1.2.2.2, length in terms of contiguously allocated resource blocks (L_RBs) shall not
        // exceed N_BWP − RB_start.
        // TODO:
        //  Adapt nof_req_prbs when using USS, i.e. L'_RBs shall not exceed N^{DL,INITIAL_BWP}_RB - RB'_start
        //  where L'_RBs = LRBs / K, RB'_start = RB_start / K, K is maximum value from set {1, 2, 4, 8}
        //  satisfying the condition K <= floor(N^{DL,ACTIVE_BWP}_RB / N^{DL,INITIAL_BWP}_RB).
        nof_req_prbs               = nof_req_prbs > (bwp_cfg.crbs.length() - bwp_cfg.crbs.start())
                                         ? bwp_cfg.crbs.length() - bwp_cfg.crbs.start()
                                         : nof_req_prbs;
        crb_interval ue_grant_crbs = find_empty_interval_of_length(used_crbs, nof_req_prbs, 0);
        if (not ue_grant_crbs.empty()) {
          pdsch_alloc.allocate_dl_grant(ue_pdsch_grant{&u,
                                                       ue_cc.cell_index,
                                                       h->id,
                                                       ss_cfg->id,
                                                       time_res,
                                                       ue_grant_crbs,
                                                       dci_dl_format::f1_0,
                                                       aggregation_level::n4});
          return true;
        }
      }
    }
  }
  return false;
}

/// Allocate UE PUSCH grant.
static bool alloc_ul_ue(const ue& u, ue_pusch_allocator& pusch_alloc, bool is_retx)
{
  unsigned pending_newtx_bytes = 0;
  if (not is_retx) {
    pending_newtx_bytes = u.pending_ul_newtx_bytes();
    if (pending_newtx_bytes == 0) {
      return false;
    }
  }

  // Prioritize PCell over SCells.
  for (unsigned i = 0; i != u.nof_cells(); ++i) {
    const ue_cell&               ue_cc       = u.get_cell(to_ue_cell_index(i));
    const ue_cell_configuration& ue_cell_cfg = ue_cc.cfg();
    const ul_harq_process*       h           = nullptr;
    if (is_retx) {
      h = ue_cc.harqs.find_pending_ul_retx();
    } else {
      h = ue_cc.harqs.find_empty_ul_harq();
    }
    if (h == nullptr) {
      continue;
    }

    const search_space_configuration* ss_cfg = ue_cell_cfg.find_search_space(to_search_space_id(2));
    if (ss_cfg == nullptr) {
      return false;
    }

    // See TS 38.212, 7.3.1.0 DCI size alignment.
    const bwp_uplink_common& bwp_ul = ue_cell_cfg.ul_bwp_common(
        ss_cfg->type == search_space_configuration::type_t::common ? to_bwp_id(0) : ue_cc.active_bwp_id());

    unsigned          time_res = 0; // TODO: Find best candidate.
    unsigned          k2       = bwp_ul.pusch_cfg_common->pusch_td_alloc_list[time_res].k2; // TODO: Take from config.
    ofdm_symbol_range pusch_symbols          = bwp_ul.pusch_cfg_common->pusch_td_alloc_list[time_res].symbols;
    const cell_slot_resource_grid& grid      = pusch_alloc.ul_resource_grid(ue_cc.cell_index, k2);
    prb_bitmap                     used_crbs = grid.used_crbs(bwp_ul.generic_params, pusch_symbols);
    unsigned                       nof_req_prbs =
        is_retx ? h->last_tx_params().prbs.prbs().length() : ue_cc.required_ul_prbs(time_res, pending_newtx_bytes);
    // As per TS 38.214, 6.1.2.2.2, length in terms of contiguously allocated resource blocks (L_RBs) shall not
    // exceed N_BWP − RB_start.
    // TODO:
    //  Adapt nof_req_prbs when using USS, i.e. L'_RBs shall not exceed N^{UL,INITIAL_BWP}_RB - RB'_start
    //  where L'_RBs = LRBs / K, RB'_start = RB_start / K, K is maximum value from set {1, 2, 4, 8}
    //  satisfying the condition K <= floor(N^{UL,ACTIVE_BWP}_RB / N^{UL,INITIAL_BWP}_RB).
    nof_req_prbs = nof_req_prbs > (bwp_ul.generic_params.crbs.length() - bwp_ul.generic_params.crbs.start())
                       ? bwp_ul.generic_params.crbs.length() - bwp_ul.generic_params.crbs.start()
                       : nof_req_prbs;
    crb_interval ue_grant_crbs = find_empty_interval_of_length(used_crbs, nof_req_prbs, 0);
    if (not ue_grant_crbs.empty()) {
      pusch_alloc.allocate_ul_grant(ue_pusch_grant{
          &u, ue_cc.cell_index, h->id, ue_grant_crbs, pusch_symbols, k2, to_search_space_id(2), aggregation_level::n4});
    }
  }
  return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void scheduler_time_rr::dl_sched(ue_pdsch_allocator& pdsch_alloc, const ue_list& ues, bool is_retx)
{
  if (not is_retx) {
    // Increment Round-robin counter to prioritize different UEs.
    rr_count++;
  }

  auto tx_ue_function = [&pdsch_alloc, is_retx](const ue& u) { return alloc_dl_ue(u, pdsch_alloc, is_retx); };
  if (round_robin_apply(ues, rr_count, tx_ue_function)) {
    return;
  }
}

void scheduler_time_rr::ul_sched(ue_pusch_allocator& pusch_alloc, const ue_list& ues, bool is_retx)
{
  if (not is_retx) {
    // Increment Round-robin counter to prioritize different UEs.
    rr_count++;
  }

  auto tx_ue_function = [&pusch_alloc, is_retx](const ue& u) { return alloc_ul_ue(u, pusch_alloc, is_retx); };
  if (round_robin_apply(ues, rr_count, tx_ue_function)) {
    return;
  }
}
