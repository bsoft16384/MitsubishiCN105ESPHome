#pragma once

#include <cstdint>

/**
 * Pure scheduling policy for the CN105 main loop.
 *
 * This header deliberately has no ESPHome (or other project) dependency so the
 * decision logic can be exercised by the unit test suite. CN105Climate::loop()
 * is a thin dispatcher over decide_loop_action(); the timing helpers below are
 * the single source of truth for the windows involved.
 *
 * Background: writing settings used to take unconditional priority over
 * starting an info cycle, and every write pushed the next cycle further out
 * (defer_cycle). Since current_settings_ is only refreshed by an info cycle, a
 * mismatch that re-armed wanted_settings_ on a timer could starve the cycle
 * forever — the unit then never read anything back and, because the remote
 * temperature packet is only written at the end of a cycle, never learned the
 * room temperature. The policy below breaks that loop in three places.
 */
namespace cn105_policy {

/// Extra time on top of the poll interval before an unacknowledged command is re-issued.
inline constexpr uint32_t COMMAND_SETTLE_MARGIN_MS = 2500;
/// Extra time on top of the poll interval before a missing cycle counts as starved.
inline constexpr uint32_t CYCLE_STARVATION_MARGIN_MS = 2000;
/// Extra time on top of the starvation threshold before a queued remote temperature is forced out.
inline constexpr uint32_t REMOTE_TEMP_STALL_MARGIN_MS = 3000;

/**
 * How long to wait for a command to be reflected in current_settings_ before
 * issuing it again.
 *
 * A write defers the next cycle, the cycle then needs a full update interval to
 * become eligible, and the 0x02 response only lands once it has started — so the
 * window has to cover two intervals plus slack. Re-issuing sooner cannot help:
 * nothing can have refreshed current_settings_ yet.
 */
inline uint32_t command_settle_window_ms(uint32_t update_interval_ms) {
  return 2 * update_interval_ms + COMMAND_SETTLE_MARGIN_MS;
}

/**
 * True when the same physical target was already commanded recently enough that
 * a repeat would be pure noise (and would defer the cycle that is supposed to
 * confirm it).
 */
inline bool should_suppress_repeat_command(bool has_previous_command, bool same_target, uint32_t now_ms,
                                           uint32_t last_command_ms, uint32_t update_interval_ms) {
  if (!has_previous_command || !same_target) {
    return false;
  }
  return (now_ms - last_command_ms) < command_settle_window_ms(update_interval_ms);
}

/// How long without a completed cycle before reads take priority over writes.
inline uint32_t cycle_starvation_threshold_ms(uint32_t update_interval_ms) {
  return 3 * update_interval_ms + CYCLE_STARVATION_MARGIN_MS;
}

/**
 * True when no info cycle has completed for long enough that starting one must
 * outrank any pending write. Writes are only delayed by one loop iteration:
 * once the cycle completes, last_cycle_end_ms moves and writes resume.
 */
inline bool is_cycle_starved(uint32_t now_ms, uint32_t last_cycle_end_ms, uint32_t update_interval_ms) {
  return (now_ms - last_cycle_end_ms) > cycle_starvation_threshold_ms(update_interval_ms);
}

/// How long a queued remote temperature may wait on the cycle scheduler before being sent directly.
inline uint32_t remote_temp_stall_threshold_ms(uint32_t update_interval_ms) {
  return cycle_starvation_threshold_ms(update_interval_ms) + REMOTE_TEMP_STALL_MARGIN_MS;
}

/**
 * Safety net: the remote temperature is normally written by terminate_cycle(),
 * so anything that stops cycles also stops the heatpump learning the room
 * temperature — it silently falls back to its own internal sensor. If a queued
 * send has been waiting longer than the stall threshold, write it directly.
 */
inline bool should_force_remote_temp_send(bool send_pending, uint32_t now_ms, uint32_t pending_since_ms,
                                          uint32_t last_send_ms, uint32_t update_interval_ms) {
  if (!send_pending) {
    return false;
  }
  if ((now_ms - pending_since_ms) < remote_temp_stall_threshold_ms(update_interval_ms)) {
    return false;
  }
  // Respect the same write throttle as the other senders.
  return (now_ms - last_send_ms) > 300;
}

/**
 * Mirrors CycleManagement::has_update_interval_passed(), including its guard
 * against last_complete_cycle_ms being pushed into the future by defer_cycle().
 */
inline bool has_update_interval_passed(uint32_t now_ms, uint32_t last_complete_cycle_ms, uint32_t update_interval_ms) {
  if (now_ms < last_complete_cycle_ms) {
    return false;
  }
  return (now_ms - last_complete_cycle_ms) > update_interval_ms;
}

/// What loop() should do this iteration.
enum class LoopAction {
  IDLE,
  CHECK_CYCLE_TIMEOUT,
  SEND_REMOTE_TEMP,
  SEND_SETTINGS,
  SEND_RUN_STATES,
  SET_FUNCTIONS,
  START_CYCLE,
};

struct LoopInputs {
  uint32_t now_ms = 0;
  uint32_t update_interval_ms = 0;
  bool cycle_running = false;
  /// Timestamp of the last completed cycle, never moved by defer_cycle().
  uint32_t last_cycle_end_ms = 0;
  /// Cycle eligibility clock, which defer_cycle() may push into the future.
  uint32_t last_complete_cycle_ms = 0;
  bool wanted_settings_changed = false;
  bool wanted_run_states_changed = false;
  bool set_functions_pending = false;
  bool remote_temp_send_pending = false;
  uint32_t remote_temp_pending_since_ms = 0;
  uint32_t last_send_ms = 0;
};

/**
 * Decides the single action loop() takes per iteration.
 *
 * Priority: an in-flight cycle is left alone, a stalled remote temperature wins
 * over everything else (it is the one packet with a physical safety impact),
 * then writes — unless cycles are starved, in which case reading wins until a
 * cycle completes.
 */
inline LoopAction decide_loop_action(const LoopInputs &in) {
  if (in.cycle_running) {
    return LoopAction::CHECK_CYCLE_TIMEOUT;
  }

  if (should_force_remote_temp_send(in.remote_temp_send_pending, in.now_ms, in.remote_temp_pending_since_ms,
                                    in.last_send_ms, in.update_interval_ms)) {
    return LoopAction::SEND_REMOTE_TEMP;
  }

  const bool starved = is_cycle_starved(in.now_ms, in.last_cycle_end_ms, in.update_interval_ms);

  if (!starved) {
    if (in.wanted_settings_changed) {
      return LoopAction::SEND_SETTINGS;
    }
    if (in.wanted_run_states_changed) {
      return LoopAction::SEND_RUN_STATES;
    }
    if (in.set_functions_pending) {
      return LoopAction::SET_FUNCTIONS;
    }
  }

  if (starved || has_update_interval_passed(in.now_ms, in.last_complete_cycle_ms, in.update_interval_ms)) {
    return LoopAction::START_CYCLE;
  }

  return LoopAction::IDLE;
}

}  // namespace cn105_policy
