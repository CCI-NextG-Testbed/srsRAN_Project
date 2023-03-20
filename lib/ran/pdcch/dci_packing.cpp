/*
 *
 * Copyright 2021-2023 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include "srsran/ran/pdcch/dci_packing.h"
#include "srsran/support/math_utils.h"

using namespace srsran;

// Computes the number of information bits before padding for a DCI format 0_0 message.
static dci_0_0_size dci_f0_0_bits_before_padding(unsigned N_rb_ul_bwp)
{
  dci_0_0_size sizes = {};

  unsigned nof_bits = 0;

  // Identifier for DCI formats - 1 bit.
  ++nof_bits;

  // Frequency domain resource assignment. Number of bits as per TS38.214 Section 6.1.2.2.2.
  sizes.freq_resource = units::bits(log2_ceil(N_rb_ul_bwp * (N_rb_ul_bwp + 1) / 2));
  nof_bits += sizes.freq_resource.value();

  // Time domain resource assignment - 4 bit.
  nof_bits += 4;

  // Frequency hopping flag - 1 bit.
  ++nof_bits;

  // Modulation and coding scheme - 5 bit.
  nof_bits += 5;

  // New data indicator - 1 bit.
  ++nof_bits;

  // Redundancy version - 2 bit.
  nof_bits += 2;

  // HARQ process number - 4 bit.
  nof_bits += 4;

  // TPC command for scheduled PUSCH - 2 bit.
  nof_bits += 2;

  sizes.total = units::bits(nof_bits);

  return sizes;
}

// Computes the number of information bits before padding for a DCI format 1_0 message.
static dci_1_0_size dci_f1_0_bits_before_padding(unsigned N_rb_dl_bwp)
{
  dci_1_0_size sizes = {};

  // Contribution to the DCI payload size that is fixed. It is the same number of bits for all format 1_0 variants.
  sizes.total = units::bits(28U);

  // Frequency domain resource assignment. Number of bits as per TS38.214 Section 5.1.2.2.2.
  sizes.freq_resource = units::bits(log2_ceil(N_rb_dl_bwp * (N_rb_dl_bwp + 1) / 2));
  sizes.total += sizes.freq_resource;

  return sizes;
}

dci_sizes srsran::get_dci_sizes(const dci_size_config& config)
{
  dci_sizes final_sizes = {};

  // Step 0
  // - Determine DCI format 0_0 monitored in a common search space according to clause 7.3.1.1.1 where N_UL_BWP_RB is
  // given by the size of the initial UL bandwidth part.
  dci_0_0_size format0_0_info_bits_common = dci_f0_0_bits_before_padding(config.ul_bwp_initial_bw);

  // - Determine DCI format 1_0 monitored in a common search space according to clause 7.3.1.2.1 where N_DL_BWP_RB given
  // by:
  //   - the size of CORESET 0 if CORESET 0 is configured for the cell
  //   - the size of initial DL bandwidth part if CORESET 0 is not configured for the cell.
  dci_1_0_size format1_0_info_bits_common =
      dci_f1_0_bits_before_padding((config.coreset0_bw != 0) ? config.coreset0_bw : config.dl_bwp_initial_bw);

  final_sizes.format0_0_common_size = format0_0_info_bits_common;
  final_sizes.format1_0_common_size = format1_0_info_bits_common;

  // - If DCI format 0_0 is monitored in common search space and if the number of information bits in the DCI format 0_0
  // prior to padding is less than the payload size of the DCI format 1_0 monitored in common search space for
  // scheduling the same serving cell, a number of zero padding bits are generated for the DCI format 0_0 until the
  // payload size equals that of the DCI format 1_0.
  if (format0_0_info_bits_common.total < format1_0_info_bits_common.total) {
    // The number of padding bits is computed here, including the single bit UL/SUL field. This field is located after
    // the padding, and it must only be included if the format 1_0 payload has a larger amount of bits before the
    // padding bits than the format 0_0 payload. Therefore, the UL/SUL can be though of as a field that takes the space
    // of the last padding bit within the format 0_0 payload, if present. See TS38.212 Sections 7.3.1.0 and 7.3.1.1.1.
    final_sizes.format0_0_common_size.padding_incl_ul_sul =
        format1_0_info_bits_common.total - format0_0_info_bits_common.total;

    // Update the DCI format 0_0 total payload size.
    final_sizes.format0_0_common_size.total += final_sizes.format0_0_common_size.padding_incl_ul_sul;
  }

  // - If DCI format 0_0 is monitored in common search space and if the number of information bits in the DCI format 0_0
  // prior to truncation is larger than the payload size of the DCI format 1_0 monitored in common search space for
  // scheduling the same serving cell, the bitwidth of the frequency domain resource assignment field in the DCI format
  // 0_0 is reduced by truncating the first few most significant bits such that the size of DCI format 0_0 equals the
  // size of the DCI format 1_0.
  if (format0_0_info_bits_common.total > format1_0_info_bits_common.total) {
    units::bits nof_truncated_bits = format0_0_info_bits_common.total - format1_0_info_bits_common.total;
    final_sizes.format0_0_common_size.freq_resource -= nof_truncated_bits;
    final_sizes.format0_0_common_size.total -= nof_truncated_bits;
  }

  srsran_assert(final_sizes.format1_0_common_size.total == final_sizes.format0_0_common_size.total,
                "DCI format 0_0 and 1_0 payload size must match");

  // Step 1
  // - Determine DCI format 0_0 monitored in a UE-specific search space according to clause 7.3.1.1.1 where N_UL_BWP_RB
  // is the size of the active UL bandwidth part.
  dci_0_0_size format0_0_info_bits_ue = dci_f0_0_bits_before_padding(config.ul_bwp_active_bw);

  // - Determine DCI format 1_0 monitored in a UE-specific search space according to clause 7.3.1.2.1 where N_DL_BWP_RB
  // is the size of the active DL bandwidth part.
  dci_1_0_size format1_0_info_bits_ue = dci_f1_0_bits_before_padding(config.dl_bwp_active_bw);

  final_sizes.format0_0_ue_size = format0_0_info_bits_ue;
  final_sizes.format1_0_ue_size = format1_0_info_bits_ue;

  // - For a UE configured with supplementaryUplink in ServingCellConfig in a cell, if PUSCH is configured to be
  // transmitted on both the SUL and the non-SUL of the cell and if the number of information bits in DCI format 0_0 in
  // UE-specific search space for the SUL is not equal to the number of information bits in DCI format 0_0 in
  // UE-specific search space for the non-SUL, a number of zero padding bits are generated for the smaller DCI format
  // 0_0 until the payload size equals that of the larger DCI format 0_0.

  // Not implemented.

  // - If DCI format 0_0 is monitored in UE-specific search space and if the number of information bits in the DCI
  // format 0_0 prior to padding is less than the payload size of the DCI format 1_0 monitored in UE-specific search
  // space for scheduling the same serving cell, a number of zero padding bits are generated for the DCI format 0_0
  // until the payload size equals that of the DCI format 1_0.
  if (format0_0_info_bits_ue.total < format1_0_info_bits_ue.total) {
    units::bits nof_padding_bits_incl_ul_sul          = format1_0_info_bits_ue.total - format0_0_info_bits_ue.total;
    final_sizes.format0_0_ue_size.padding_incl_ul_sul = nof_padding_bits_incl_ul_sul;
    final_sizes.format0_0_ue_size.total += nof_padding_bits_incl_ul_sul;
  }

  // - If DCI format 1_0 is monitored in UE-specific search space and if the number of information bits in the DCI
  // format 1_0 prior to padding is less than the payload size of the DCI format 0_0 monitored in UE-specific search
  // space for scheduling the same serving cell, zeros shall be appended to the DCI format 1_0 until the payload size
  // equals that of the DCI format 0_0
  if (format1_0_info_bits_ue.total < format0_0_info_bits_ue.total) {
    units::bits nof_padding_bits          = format0_0_info_bits_ue.total - format1_0_info_bits_ue.total;
    final_sizes.format1_0_ue_size.padding = nof_padding_bits;
    final_sizes.format1_0_ue_size.total += final_sizes.format1_0_ue_size.padding;
  }

  srsran_assert(final_sizes.format1_0_ue_size.total == final_sizes.format0_0_ue_size.total,
                "DCI format 0_0 and 1_0 final_sizes must match");

  return final_sizes;
}

dci_payload srsran::dci_0_0_c_rnti_pack(const dci_0_0_c_rnti_configuration& config)
{
  dci_payload payload;
  units::bits frequency_resource_nof_bits = config.payload_size.freq_resource;

  // Identifier for DCI formats - 1 bit. This field is always 0, indicating an UL DCI format.
  payload.push_back(0x00U, 1);

  if (config.frequency_hopping_flag) {
    // Assert that the number of bits used to pack the frequency hopping offset is valid.
    srsran_assert((config.N_ul_hop == 1) || (config.N_ul_hop == 2),
                  "DCI frequency offset number of bits must be either 1 or 2");

    // Assert that the frequency resource field has enough bits to include the frequency hopping offset.
    srsran_assert(config.N_ul_hop < frequency_resource_nof_bits.value(),
                  "The frequency resource field must have enough bits to hold the frequency hopping offset");

    // Assert that the frequency hopping offset can be packed with the allocated bits.
    srsran_assert(config.hopping_offset < (1U << config.N_ul_hop),
                  "DCI frequency offset value ({}) cannot be packed with the allocated number of bits ({})",
                  config.hopping_offset,
                  config.N_ul_hop);

    // Truncate the frequency resource allocation field.
    frequency_resource_nof_bits -= units::bits(config.N_ul_hop);

    // Frequency hopping offset - N_ul_hop bits.
    payload.push_back(config.hopping_offset, config.N_ul_hop);
  }

  // Frequency domain resource assignment - frequency_resource_nof_bits bits.
  payload.push_back(config.frequency_resource, frequency_resource_nof_bits.value());

  // Time domain resource assignment - 4 bit.
  payload.push_back(config.time_resource, 4);

  // Frequency hopping flag - 1 bit.
  payload.push_back(config.frequency_hopping_flag, 1);

  // Modulation coding scheme - 5 bits.
  payload.push_back(config.modulation_coding_scheme, 5);

  // New data indicator - 1 bit.
  payload.push_back(config.new_data_indicator, 1);

  // Redundancy version - 2 bit.
  payload.push_back(config.redundancy_version, 2);

  // HARQ process number - 4 bit.
  payload.push_back(config.harq_process_number, 4);

  // TPC command for scheduled PUSCH - 2 bit.
  payload.push_back(config.tpc_command, 2);

  if (config.payload_size.padding_incl_ul_sul.value() > 0) {
    if (config.ul_sul_indicator.has_value()) {
      // UL/SUL field is included if it is present in the DCI message and the number of DCI format 1_0 bits before
      // padding is larger than the number of DCI format 0_0 bits before padding.
      constexpr unsigned nof_ul_sul_bit = 1U;
      // Padding bits, if necessary, as per TS38.212 Section 7.3.1.0.
      payload.push_back(0x00U, config.payload_size.padding_incl_ul_sul.value() - nof_ul_sul_bit);

      // UL/SUL indicator - 1 bit.
      payload.push_back(config.ul_sul_indicator.value(), nof_ul_sul_bit);
    } else {
      // UL/SUL field is not included otherwise.
      payload.push_back(0x00U, config.payload_size.padding_incl_ul_sul.value());
    }
  }

  // Assert total payload size.
  srsran_assert(payload.size() == config.payload_size.total.value(),
                "Constructed payload size (i.e., {}) does not match expected size (i.e., {}).",
                payload.size(),
                config.payload_size.total);

  return payload;
}

dci_payload srsran::dci_0_0_tc_rnti_pack(const dci_0_0_tc_rnti_configuration& config)
{
  units::bits frequency_resource_nof_bits = config.payload_size.freq_resource;
  dci_payload payload;

  // Identifier for DCI formats - 1 bit. This field is always 0, indicating an UL DCI format.
  payload.push_back(0x00U, 1);

  if (config.frequency_hopping_flag) {
    // Assert that the number of bits used to pack the frequency hopping offset is valid.
    srsran_assert((config.N_ul_hop == 1) || (config.N_ul_hop == 2),
                  "DCI frequency offset number of bits must be either 1 or 2");

    // Assert that the frequency resource field has enough bits to include the frequency hopping offset.
    srsran_assert(config.N_ul_hop < frequency_resource_nof_bits.value(),
                  "The frequency resource field must have enough bits to hold the frequency hopping offset");

    // Assert that the frequency hopping offset can be packed with the allocated bits.
    srsran_assert(config.hopping_offset < (1U << config.N_ul_hop),
                  "DCI frequency offset value ({}) cannot be packed with the allocated number of bits ({})",
                  config.hopping_offset,
                  config.N_ul_hop);

    // Truncate the frequency resource allocation field. to fit the frequency hopping offset.
    frequency_resource_nof_bits -= units::bits(config.N_ul_hop);

    // Frequency hopping offset - N_ul_hop bits.
    payload.push_back(config.hopping_offset, config.N_ul_hop);
  }

  // Frequency domain resource assignment - frequency_resource_nof_bits bits.
  payload.push_back(config.frequency_resource, frequency_resource_nof_bits.value());

  // Time domain resource assignment - 4 bit.
  payload.push_back(config.time_resource, 4);

  // Frequency hopping flag - 1 bit.
  payload.push_back(config.frequency_hopping_flag, 1);

  // Modulation coding scheme - 5 bits.
  payload.push_back(config.modulation_coding_scheme, 5);

  // New data indicator - 1 bit, reserved.
  payload.push_back(0x00U, 1);

  // Redundancy version - 2 bit.
  payload.push_back(config.redundancy_version, 2);

  // HARQ process number - 4 bit, reserved.
  payload.push_back(0x00U, 4);

  // TPC command for scheduled PUSCH - 2 bit.
  payload.push_back(config.tpc_command, 2);

  if (config.payload_size.padding_incl_ul_sul.value() > 0) {
    // Padding bits, including UL/SUL reserved field.
    payload.push_back(0x00U, config.payload_size.padding_incl_ul_sul.value());
  }

  // Assert total payload size.
  srsran_assert(payload.size() == config.payload_size.total.value(),
                "Constructed payload size (i.e., {}) does not match expected size (i.e., {}).",
                payload.size(),
                config.payload_size.total.value());

  return payload;
}

dci_payload srsran::dci_1_0_c_rnti_pack(const dci_1_0_c_rnti_configuration& config)
{
  dci_payload payload;

  // Identifier for DCI formats - 1 bit. This field is always 1, indicating a DL DCI format.
  payload.push_back(0x01U, 1);

  // Frequency domain resource assignment - frequency_resource_nof_bits bits.
  payload.push_back(config.frequency_resource, config.payload_size.freq_resource.value());

  // Time domain resource assignment - 4 bit.
  payload.push_back(config.time_resource, 4);

  // VRB-to-PRB mapping - 1 bit.
  payload.push_back(config.vrb_to_prb_mapping, 1);

  // Modulation coding scheme - 5 bits.
  payload.push_back(config.modulation_coding_scheme, 5);

  // New data indicator - 1 bit.
  payload.push_back(config.new_data_indicator, 1);

  // Redundancy version - 2 bit.
  payload.push_back(config.redundancy_version, 2);

  // HARQ process number - 4 bit.
  payload.push_back(config.harq_process_number, 4);

  // Downlink assignment index - 2 bit.
  payload.push_back(config.dl_assignment_index, 2);

  // TPC command for scheduled PUCCH - 2 bit.
  payload.push_back(config.tpc_command, 2);

  // PUCCH resource indicator - 3 bit.
  payload.push_back(config.pucch_resource_indicator, 3);

  // PDSCH to HARQ feedback timing indicator - 3 bit.
  payload.push_back(config.pdsch_harq_fb_timing_indicator, 3);

  // Padding - nof_padding_bits bits.
  payload.push_back(0x00U, config.payload_size.padding.value());

  // Assert total payload size.
  srsran_assert(payload.size() == config.payload_size.total.value(),
                "Constructed payload size (i.e., {}) does not match expected size (i.e., {}).",
                payload.size(),
                config.payload_size.total);

  return payload;
}

dci_payload srsran::dci_1_0_p_rnti_pack(const dci_1_0_p_rnti_configuration& config)
{
  units::bits frequency_resource_nof_bits(log2_ceil(config.N_rb_dl_bwp * (config.N_rb_dl_bwp + 1) / 2));
  dci_payload payload;

  // Short Message Indicator - 2 bits.
  switch (config.short_messages_indicator) {
    case dci_1_0_p_rnti_configuration::payload_info::scheduling_information:
      payload.push_back(0b01U, 2);
      break;
    case dci_1_0_p_rnti_configuration::payload_info::short_messages:
      payload.push_back(0b10U, 2);
      break;
    case dci_1_0_p_rnti_configuration::payload_info::both:
      payload.push_back(0b11U, 2);
      break;
  }

  // Short Messages - 8 bits.
  if (config.short_messages_indicator == dci_1_0_p_rnti_configuration::payload_info::scheduling_information) {
    // If only the scheduling information for paging is carried, this bit field is reserved.
    payload.push_back(0x00U, 8);
  } else {
    payload.push_back(config.short_messages, 8);
  }

  if (config.short_messages_indicator == dci_1_0_p_rnti_configuration::payload_info::short_messages) {
    // If only the short message is carried, the scheduling information for paging bit fields are reserved.
    payload.push_back(0x00U, frequency_resource_nof_bits.value() + 12);
  } else {
    // Frequency domain resource assignment - frequency_resource_nof_bits bits.
    payload.push_back(config.frequency_resource, frequency_resource_nof_bits.value());

    // Time domain resource assignment - 4 bits.
    payload.push_back(config.time_resource, 4);

    // VRB-to-PRB mapping - 1 bit.
    payload.push_back(config.vrb_to_prb_mapping, 1);

    // Modulation and coding scheme - 5 bits.
    payload.push_back(config.modulation_coding_scheme, 5);

    // Transport Block scaling - 2 bits.
    payload.push_back(config.tb_scaling, 2);
  }

  // Reserved bits: 6 bits.
  payload.push_back(0x00U, 6);

  return payload;
}

dci_payload srsran::dci_1_0_si_rnti_pack(const dci_1_0_si_rnti_configuration& config)
{
  units::bits frequency_resource_nof_bits(log2_ceil(config.N_rb_dl_bwp * (config.N_rb_dl_bwp + 1) / 2));
  dci_payload payload;

  // Frequency domain resource assignment - frequency_resource_nof_bits bits.
  payload.push_back(config.frequency_resource, frequency_resource_nof_bits.value());

  // Time domain resource assignment - 4 bit.
  payload.push_back(config.time_resource, 4);

  // VRB-to-PRB mapping - 1 bit.
  payload.push_back(config.vrb_to_prb_mapping, 1);

  // Modulation coding scheme - 5 bits.
  payload.push_back(config.modulation_coding_scheme, 5);

  // Redundancy version - 2 bits.
  payload.push_back(config.redundancy_version, 2);

  // System information indicator - 1 bit.
  payload.push_back(config.system_information_indicator, 1);

  // Reserved bits - 15 bit.
  payload.push_back(0x00U, 15);

  return payload;
}

dci_payload srsran::dci_1_0_ra_rnti_pack(const dci_1_0_ra_rnti_configuration& config)
{
  units::bits frequency_resource_nof_bits(log2_ceil(config.N_rb_dl_bwp * (config.N_rb_dl_bwp + 1) / 2));
  dci_payload payload;

  // Frequency domain resource assignment - frequency_resource_nof_bits bits.
  payload.push_back(config.frequency_resource, frequency_resource_nof_bits.value());

  // Time domain resource assignment - 4 bits.
  payload.push_back(config.time_resource, 4);

  // VRB-to-PRB mapping - 1 bit.
  payload.push_back(config.vrb_to_prb_mapping, 1);

  // Modulation and coding scheme - 5 bits.
  payload.push_back(config.modulation_coding_scheme, 5);

  // Transport Block scaling - 2 bits.
  payload.push_back(config.tb_scaling, 2);

  // Reserved bits - 16 bits.
  payload.push_back(0x00U, 16);

  return payload;
}

dci_payload srsran::dci_1_0_tc_rnti_pack(const dci_1_0_tc_rnti_configuration& config)
{
  units::bits frequency_resource_nof_bits(log2_ceil(config.N_rb_dl_bwp * (config.N_rb_dl_bwp + 1) / 2));
  dci_payload payload;

  // Identifier for DCI formats - 1 bit. This field is always 1, indicating a DL DCI format.
  payload.push_back(0x01U, 1);

  // Frequency domain resource assignment - frequency_resource_nof_bits bits.
  payload.push_back(config.frequency_resource, frequency_resource_nof_bits.value());

  // Time domain resource assignment - 4 bit.
  payload.push_back(config.time_resource, 4);

  // VRB-to-PRB mapping - 1 bit.
  payload.push_back(config.vrb_to_prb_mapping, 1);

  // Modulation coding scheme - 5 bits.
  payload.push_back(config.modulation_coding_scheme, 5);

  // New data indicator - 1 bit.
  payload.push_back(config.new_data_indicator, 1);

  // Redundancy version - 2 bit.
  payload.push_back(config.redundancy_version, 2);

  // HARQ process number - 4 bit.
  payload.push_back(config.harq_process_number, 4);

  // Downlink assignment index - 2 bit, reserved.
  payload.push_back(0x00U, 2);

  // TPC command for scheduled PUCCH - 2 bit.
  payload.push_back(config.tpc_command, 2);

  // PUCCH resource indicator - 3 bit.
  payload.push_back(config.pucch_resource_indicator, 3);

  // PDSCH to HARQ feedback timing indicator - 3 bit.
  payload.push_back(config.pdsch_harq_fb_timing_indicator, 3);

  return payload;
}

dci_payload srsran::dci_rar_pack(const dci_rar_configuration& config)
{
  dci_payload payload;

  // Frequency hopping flag - 1 bit.
  payload.push_back(config.frequency_hopping_flag, 1);

  // PUSCH frequency resource allocation - 14 bits.
  payload.push_back(config.frequency_resource, 14);

  // PUSCH time resource allocation - 4 bits.
  payload.push_back(config.time_resource, 4);

  // Modulation and coding scheme - 4 bits.
  payload.push_back(config.modulation_coding_scheme, 4);

  // TPC command for PUSCH - 3 bits.
  payload.push_back(config.tpc, 3);

  // CSI request - 1 bit.
  payload.push_back(config.csi_request, 1);

  return payload;
}
