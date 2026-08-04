/// test_scheduling_policy.cpp — Main-loop scheduling policy (cn105_policy).
///
/// Regression coverage for the livelock where a settings write starved the info
/// cycle that was supposed to confirm it: current_settings_ never refreshed, so
/// the mismatch re-armed forever, and the remote temperature packet — which is
/// only written at the end of a cycle — was never sent. The heatpump then ran on
/// its own internal sensor and overshot the setpoint by several degrees.

#include <gtest/gtest.h>

#include "scheduling_policy.h"

using namespace cn105_policy;

namespace {

// ── Timing helpers ────────────────────────────────────────────────────────────

TEST(SchedulingPolicyWindows, SettleWindowCoversTwoPollIntervals) {
  // A write defers the next cycle, the cycle then needs a full interval to become
  // eligible, and the 0x02 readback only lands once it has started.
  EXPECT_GT(command_settle_window_ms(2500), 2u * 2500u);
  EXPECT_GT(command_settle_window_ms(5000), 2u * 5000u);
}

TEST(SchedulingPolicyWindows, ThresholdsAreOrdered) {
  // The starvation guard must get a chance to fix things before the remote-temp
  // safety net resorts to writing outside the cycle.
  for (uint32_t interval : {1000u, 2500u, 5000u, 30000u}) {
    EXPECT_LT(cycle_starvation_threshold_ms(interval), remote_temp_stall_threshold_ms(interval)) << interval;
  }
}

TEST(SchedulingPolicyWindows, StarvationThresholdExceedsHealthyCyclePeriod) {
  // A healthy period is: cycle duration + defer (<=1500ms) + one update interval.
  // The guard must not fire during normal operation.
  const uint32_t generous_cycle_duration = 2000;
  for (uint32_t interval : {1000u, 2500u, 5000u}) {
    EXPECT_GT(cycle_starvation_threshold_ms(interval), generous_cycle_duration + 1500 + interval) << interval;
  }
}

// ── Repeat-command suppression (fix 1) ────────────────────────────────────────

TEST(RepeatCommandSuppression, FirstCommandIsNeverSuppressed) {
  EXPECT_FALSE(should_suppress_repeat_command(/*has_previous=*/false, /*same_target=*/false, 100000, 0, 5000));
}

TEST(RepeatCommandSuppression, DifferentTargetIsSentImmediately) {
  // A user change or LTP engaging must not be delayed by the settle window.
  EXPECT_FALSE(should_suppress_repeat_command(true, /*same_target=*/false, 10100, 10000, 5000));
}

TEST(RepeatCommandSuppression, IdenticalTargetIsHeldForTheSettleWindow) {
  const uint32_t interval = 5000;
  const uint32_t issued_at = 10000;
  const uint32_t window = command_settle_window_ms(interval);

  // The 5s evaluation tick that used to re-issue the command lands inside the window.
  EXPECT_TRUE(should_suppress_repeat_command(true, true, issued_at + 5000, issued_at, interval));
  EXPECT_TRUE(should_suppress_repeat_command(true, true, issued_at + window - 1, issued_at, interval));
}

TEST(RepeatCommandSuppression, RetriesOnceTheWindowExpires) {
  // Suppression is rate limiting, not giving up: a command the unit really ignored
  // is retried after the window.
  const uint32_t interval = 5000;
  const uint32_t issued_at = 10000;
  EXPECT_FALSE(should_suppress_repeat_command(true, true, issued_at + command_settle_window_ms(interval), issued_at,
                                              interval));
}

// ── Starvation guard (fix 2) ──────────────────────────────────────────────────

TEST(CycleStarvation, NotStarvedDuringNormalPolling) {
  const uint32_t interval = 5000;
  EXPECT_FALSE(is_cycle_starved(/*now=*/16500, /*last_cycle_end=*/10000, interval));
}

TEST(CycleStarvation, StarvedAfterThreshold) {
  const uint32_t interval = 5000;
  const uint32_t last_end = 10000;
  EXPECT_TRUE(is_cycle_starved(last_end + cycle_starvation_threshold_ms(interval) + 1, last_end, interval));
}

TEST(CycleStarvation, StarvationOutranksPendingWrites) {
  LoopInputs in;
  in.update_interval_ms = 5000;
  in.last_cycle_end_ms = 10000;
  in.last_complete_cycle_ms = 60000;  // pushed into the future by defer_cycle()
  in.now_ms = in.last_cycle_end_ms + cycle_starvation_threshold_ms(in.update_interval_ms) + 1;
  in.wanted_settings_changed = true;
  in.wanted_run_states_changed = true;
  in.set_functions_pending = true;

  // Reading wins, and the deferred eligibility clock is bypassed.
  EXPECT_EQ(decide_loop_action(in), LoopAction::START_CYCLE);
}

TEST(CycleStarvation, WritesResumeOnceACycleCompletes) {
  LoopInputs in;
  in.update_interval_ms = 5000;
  in.now_ms = 100000;
  in.last_cycle_end_ms = in.now_ms - 100;  // a cycle just ended
  in.last_complete_cycle_ms = in.now_ms - 100;
  in.wanted_settings_changed = true;

  EXPECT_EQ(decide_loop_action(in), LoopAction::SEND_SETTINGS);
}

// ── Remote temperature stall net (fix 3) ──────────────────────────────────────

TEST(RemoteTempStall, NothingQueuedMeansNothingForced) {
  EXPECT_FALSE(should_force_remote_temp_send(/*pending=*/false, 999999, 0, 0, 5000));
}

TEST(RemoteTempStall, WaitsForTheStallThreshold) {
  const uint32_t interval = 5000;
  const uint32_t queued_at = 10000;
  EXPECT_FALSE(should_force_remote_temp_send(true, queued_at + 1000, queued_at, 0, interval));
  EXPECT_FALSE(should_force_remote_temp_send(true, queued_at + remote_temp_stall_threshold_ms(interval) - 1, queued_at,
                                             0, interval));
  EXPECT_TRUE(
      should_force_remote_temp_send(true, queued_at + remote_temp_stall_threshold_ms(interval) + 1, queued_at, 0,
                                    interval));
}

TEST(RemoteTempStall, RespectsTheWriteThrottle) {
  const uint32_t interval = 5000;
  const uint32_t queued_at = 10000;
  const uint32_t now = queued_at + remote_temp_stall_threshold_ms(interval) + 1;
  EXPECT_FALSE(should_force_remote_temp_send(true, now, queued_at, /*last_send=*/now - 100, interval));
  EXPECT_TRUE(should_force_remote_temp_send(true, now, queued_at, /*last_send=*/now - 400, interval));
}

TEST(RemoteTempStall, OutranksEverythingButARunningCycle) {
  LoopInputs in;
  in.update_interval_ms = 5000;
  in.now_ms = 100000;
  in.last_cycle_end_ms = in.now_ms;
  in.last_complete_cycle_ms = in.now_ms;
  in.wanted_settings_changed = true;
  in.remote_temp_send_pending = true;
  in.remote_temp_pending_since_ms = in.now_ms - remote_temp_stall_threshold_ms(in.update_interval_ms) - 1;
  in.last_send_ms = in.now_ms - 5000;

  EXPECT_EQ(decide_loop_action(in), LoopAction::SEND_REMOTE_TEMP);

  // ...but the bus stays serialized: an in-flight cycle is never interrupted.
  in.cycle_running = true;
  EXPECT_EQ(decide_loop_action(in), LoopAction::CHECK_CYCLE_TIMEOUT);
}

// ── Ordering preserved from the original loop ─────────────────────────────────

TEST(LoopOrdering, WritePriorityIsUnchangedWhenHealthy) {
  LoopInputs in;
  in.update_interval_ms = 2500;
  in.now_ms = 50000;
  in.last_cycle_end_ms = in.now_ms - 500;
  in.last_complete_cycle_ms = in.now_ms - 500;
  in.wanted_settings_changed = true;
  in.wanted_run_states_changed = true;
  in.set_functions_pending = true;
  EXPECT_EQ(decide_loop_action(in), LoopAction::SEND_SETTINGS);

  in.wanted_settings_changed = false;
  EXPECT_EQ(decide_loop_action(in), LoopAction::SEND_RUN_STATES);

  in.wanted_run_states_changed = false;
  EXPECT_EQ(decide_loop_action(in), LoopAction::SET_FUNCTIONS);
}

TEST(LoopOrdering, IdlesUntilTheIntervalElapses) {
  LoopInputs in;
  in.update_interval_ms = 2500;
  in.last_cycle_end_ms = 10000;
  in.last_complete_cycle_ms = 10000;
  in.now_ms = 11000;
  EXPECT_EQ(decide_loop_action(in), LoopAction::IDLE);

  in.now_ms = 10000 + 2501;
  EXPECT_EQ(decide_loop_action(in), LoopAction::START_CYCLE);
}

TEST(LoopOrdering, DeferredEligibilityClockInTheFutureIdles) {
  LoopInputs in;
  in.update_interval_ms = 2500;
  in.now_ms = 10000;
  in.last_cycle_end_ms = 9800;
  in.last_complete_cycle_ms = 11500;  // defer_cycle() pushed it forward
  EXPECT_EQ(decide_loop_action(in), LoopAction::IDLE);
}

// ── End-to-end regression: the observed livelock ──────────────────────────────

/// Minimal model of the driver's timing, faithful to the real call sites:
///  - loop() evaluates the physical target every 5s (component_entries.cpp)
///  - a settings write clears the flag and defers the cycle (send_wanted_settings_delegate)
///  - current_settings_ is only refreshed when a cycle completes (0x02 readback)
///  - the remote temperature is written by terminate_cycle(), at cycle end
struct DriverSim {
  uint32_t update_interval = 5000;
  uint32_t defer_delay = 1500;      // 750ms doubled on a debug-level build
  uint32_t cycle_duration = 1200;   // time for a full round of info requests
  uint32_t keepalive_interval = 20000;

  // Fix toggles, so each fix can be shown to be independently sufficient.
  bool starvation_guard = true;
  bool repeat_suppression = true;
  bool stall_net = true;

  uint32_t now = 0;
  bool cycle_running = false;
  uint32_t cycle_started_at = 0;
  uint32_t last_cycle_end_ms = 0;
  uint32_t last_complete_cycle_ms = 0;
  uint32_t last_send_ms = 0;

  bool wanted_settings_changed = false;
  bool settings_confirmed = false;  // current_settings_ matches the commanded target

  bool has_issued_command = false;
  uint32_t last_issued_command_ms = 0;

  bool remote_temp_pending = false;
  uint32_t remote_temp_pending_since = 0;

  uint32_t last_evaluation = 0;
  uint32_t last_keepalive = 0;

  int completed_cycles = 0;
  int remote_temp_sends = 0;
  int settings_writes = 0;

  void evaluate() {
    if (settings_confirmed) {
      return;
    }
    // Same physical target every time: this is the repeat the old code kept re-issuing.
    if (repeat_suppression && should_suppress_repeat_command(has_issued_command, /*same_target=*/true, now,
                                                             last_issued_command_ms, update_interval)) {
      return;
    }
    has_issued_command = true;
    last_issued_command_ms = now;
    wanted_settings_changed = true;
  }

  LoopAction decide() {
    if (cycle_running) {
      return LoopAction::CHECK_CYCLE_TIMEOUT;
    }
    if (stall_net && should_force_remote_temp_send(remote_temp_pending, now, remote_temp_pending_since, last_send_ms,
                                                   update_interval)) {
      return LoopAction::SEND_REMOTE_TEMP;
    }
    const bool starved = starvation_guard && is_cycle_starved(now, last_cycle_end_ms, update_interval);
    if (!starved && wanted_settings_changed) {
      return LoopAction::SEND_SETTINGS;
    }
    if (starved || has_update_interval_passed(now, last_complete_cycle_ms, update_interval)) {
      return LoopAction::START_CYCLE;
    }
    return LoopAction::IDLE;
  }

  void step(uint32_t dt) {
    now += dt;

    if (now - last_evaluation >= 5000) {
      last_evaluation = now;
      evaluate();
    }
    if (now - last_keepalive >= keepalive_interval) {
      last_keepalive = now;
      if (!remote_temp_pending) {
        remote_temp_pending = true;
        remote_temp_pending_since = now;
      }
    }

    switch (decide()) {
      case LoopAction::SEND_SETTINGS:
        wanted_settings_changed = false;
        last_send_ms = now;
        settings_writes++;
        last_complete_cycle_ms = now + defer_delay;  // defer_cycle()
        break;

      case LoopAction::SEND_REMOTE_TEMP:
        remote_temp_pending = false;
        remote_temp_pending_since = 0;
        last_send_ms = now;
        remote_temp_sends++;
        break;

      case LoopAction::START_CYCLE:
        cycle_running = true;
        cycle_started_at = now;
        break;

      case LoopAction::CHECK_CYCLE_TIMEOUT:
        if (now - cycle_started_at >= cycle_duration) {
          // terminate_cycle(): the remote temperature goes out here, then cycle_ended()
          if (remote_temp_pending) {
            remote_temp_pending = false;
            remote_temp_pending_since = 0;
            last_send_ms = now;
            remote_temp_sends++;
          }
          cycle_running = false;
          last_cycle_end_ms = now;
          if (last_complete_cycle_ms < now) {
            last_complete_cycle_ms = now;
          }
          completed_cycles++;
          settings_confirmed = true;  // the 0x02 response finally refreshes current_settings_
        }
        break;

      default:
        break;
    }
  }

  void run(uint32_t duration_ms, uint32_t dt = 10) {
    for (uint32_t elapsed = 0; elapsed < duration_ms; elapsed += dt) {
      step(dt);
    }
  }
};

TEST(LivelockRegression, LegacyOrderingStarvesCyclesForever) {
  // Documents the reported failure: with all three fixes off, the driver only ever
  // writes settings. No cycle completes, so current_settings_ never refreshes and
  // the remote temperature is never written to the heatpump.
  DriverSim sim;
  sim.starvation_guard = false;
  sim.repeat_suppression = false;
  sim.stall_net = false;
  sim.run(300000);  // five minutes

  EXPECT_EQ(sim.completed_cycles, 0);
  EXPECT_EQ(sim.remote_temp_sends, 0);
  EXPECT_GT(sim.settings_writes, 50);  // one every 5s, forever
}

TEST(LivelockRegression, AllFixesRecoverImmediately) {
  DriverSim sim;
  sim.run(120000);

  EXPECT_GT(sim.completed_cycles, 10);
  EXPECT_GT(sim.remote_temp_sends, 3);
  // The command is issued, confirmed by a readback, and not repeated after that.
  EXPECT_LE(sim.settings_writes, 2);
  EXPECT_TRUE(sim.settings_confirmed);
}

TEST(LivelockRegression, StarvationGuardAloneIsSufficient) {
  DriverSim sim;
  sim.repeat_suppression = false;
  sim.stall_net = false;
  sim.run(120000);

  EXPECT_GT(sim.completed_cycles, 5);
  EXPECT_GT(sim.remote_temp_sends, 3);
}

TEST(LivelockRegression, RepeatSuppressionAloneIsSufficient) {
  DriverSim sim;
  sim.starvation_guard = false;
  sim.stall_net = false;
  sim.run(120000);

  EXPECT_GT(sim.completed_cycles, 5);
  EXPECT_GT(sim.remote_temp_sends, 3);
}

TEST(LivelockRegression, StallNetDeliversTemperatureEvenIfCyclesNeverRun) {
  // Worst case: cycles are wedged for some other reason. Room temperature must
  // still reach the heatpump rather than the unit silently falling back to its own
  // sensor. Delivery is degraded — a keep-alive is only forced out once it has
  // waited the full stall threshold — but it never stops.
  DriverSim sim;
  sim.starvation_guard = false;
  sim.repeat_suppression = false;
  sim.run(120000);

  EXPECT_EQ(sim.completed_cycles, 0);
  EXPECT_GE(sim.remote_temp_sends, 3);  // roughly one per 40s in this degraded mode
  // Never a gap long enough for the unit to time out its remote temperature.
  EXPECT_LT(sim.now - sim.last_send_ms, 60000u);
}

TEST(LivelockRegression, HealthyDriverIsUnaffectedByTheGuards) {
  // Nothing pending, settings already in sync: normal polling cadence, and neither
  // the starvation guard nor the stall net ever fires.
  DriverSim sim;
  sim.settings_confirmed = true;
  sim.run(120000);

  EXPECT_EQ(sim.settings_writes, 0);
  // ~1 cycle per (cycle_duration + update_interval) = ~6.2s over 120s.
  EXPECT_GE(sim.completed_cycles, 15);
  EXPECT_LE(sim.completed_cycles, 25);
  // One keep-alive every 20s, each delivered by the normal cycle path.
  EXPECT_GE(sim.remote_temp_sends, 5);
}

}  // namespace
