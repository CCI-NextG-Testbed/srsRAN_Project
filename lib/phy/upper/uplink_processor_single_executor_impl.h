/*
 *
 * Copyright 2013-2022 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#pragma once

#include "srsgnb/phy/upper/channel_processors/prach_detector.h"
#include "srsgnb/phy/upper/uplink_processor.h"
#include "srsgnb/srslog/srslog.h"

namespace srsgnb {

class task_executor;
class upper_phy_rx_results_notifier;

/// \brief Uplink processor implementation with a single executor.
///
/// In this implementation, every task is processed by the same executor. When a task finishes, the results are notified
/// by the upper_phy_rx_results_notifier interface.
class uplink_processor_single_executor_impl : public uplink_processor
{
public:
  uplink_processor_single_executor_impl(std::unique_ptr<prach_detector> detector_,
                                        task_executor&                  executor,
                                        upper_phy_rx_results_notifier&  results_notifier);

  // See interface for documentation.
  void process_prach(const prach_buffer& buffer, const prach_buffer_context& context) override;

private:
  /// PRACH detector.
  std::unique_ptr<prach_detector> detector;
  /// Executor for the tasks generated within this uplink processor.
  task_executor& executor;
  /// Notifier of results for the processing tasks.
  upper_phy_rx_results_notifier& results_notifier;
};

} // namespace srsgnb