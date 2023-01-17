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

#include "srsgnb/adt/ring_buffer.h"
#include "srsgnb/support/executors/task_executor.h"

namespace srsgnb {

/// \brief Task worker that implements the executor interface and requires manual calls to run pending deferred tasks.
/// Useful for unit testing.
/// \note This implementation always queues the tasks, either by calling execute or defer.
class manual_task_worker_always_enqueue_tasks : public task_executor
{
public:
  manual_task_worker_always_enqueue_tasks(size_t q_size) : t_id(std::this_thread::get_id()), pending_tasks(q_size) {}

  std::thread::id get_thread_id() const { return t_id; }

  void execute(unique_task task) override { defer(std::move(task)); }

  void defer(unique_task task) override { pending_tasks.push_blocking(std::move(task)); }

  bool has_pending_tasks() const { return not pending_tasks.empty(); }

  bool is_stopped() const { return pending_tasks.is_stopped(); }

  void stop()
  {
    if (not is_stopped()) {
      pending_tasks.stop();
    }
  }

  void request_stop()
  {
    defer([this]() { stop(); });
  }

  /// Run all pending tasks until queue is emptied.
  bool run_pending_tasks()
  {
    assert_thread_id();
    bool ret = false, success = false;
    do {
      unique_task t;
      success = pending_tasks.try_pop(t);
      if (success) {
        t();
        ret = true;
      }
    } while (success);
    return ret;
  }

  /// Run next pending task if it is enqueued.
  bool try_run_next()
  {
    assert_thread_id();
    unique_task t;
    bool        success = pending_tasks.try_pop(t);
    if (not success) {
      return false;
    }
    t();
    return true;
  }

  /// Run next pending task once it is enqueued.
  bool run_next_blocking()
  {
    assert_thread_id();
    bool        success = false;
    unique_task t       = pending_tasks.pop_blocking(&success);
    if (not success) {
      return false;
    }
    t();
    return true;
  }

private:
  bool has_thread_id() const { return t_id != std::thread::id{}; }

  void assert_thread_id()
  {
    srsgnb_assert(t_id == std::this_thread::get_id(), "run() caller thread should not change.");
  }

  std::thread::id                 t_id;
  dyn_blocking_queue<unique_task> pending_tasks;
};

} // namespace srsgnb
