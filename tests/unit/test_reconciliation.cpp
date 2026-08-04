/// test_reconciliation.cpp — Tests for reconciliation_policy.h.
/// Deps: reconciliation_policy.h (production code, no mocks needed)
///
/// The reconciler runs every 5 s and on every control() call, and a wrong answer
/// either fights the user, fights the IR remote, or re-commands the unit forever
/// without converging. None of that is observable on a live device without
/// waiting minutes, which is why the decision is a pure function.
#include <gtest/gtest.h>
#include <cmath>
#include "reconciliation_policy.h"

using namespace cn105_policy;

namespace {

/// A unit sitting idle at 20 C with nothing configured: no protections, and the
/// unit already matching what the user asked for. Tests perturb one thing at a time.
ReconcileInputs baseline() {
    ReconcileInputs in;
    in.now_ms = 1'000'000;
    in.update_interval_ms = 2500;

    in.desired_power_on = true;
    in.desired_mode = HPMode::HEAT;
    in.desired_temp = 20.0f;

    in.current_power = HPPower::ON;
    in.current_mode = HPMode::HEAT;
    in.current_temperature = 20.0f;
    in.current_temp = 20.0f;

    in.ltp_enabled = false;
    in.ltp_active = false;
    in.low_temp_temp = 8.0f;
    in.low_temp_hysteresis = 4.0f;

    in.fan_stop_enabled = false;
    in.hysteresis = 0.5f;

    in.has_issued_command = false;
    in.last_issued_ms = 0;
    return in;
}

}  // namespace

// ════════════════════════════════════════════════════════════════
// Nothing to do
// ════════════════════════════════════════════════════════════════

TEST(Reconcile, NoCommandWhenUnitAlreadyMatches) {
    const auto d = decide_reconciliation(baseline());
    EXPECT_FALSE(d.has_mismatch());
    EXPECT_FALSE(d.should_command);
    EXPECT_EQ(d.protection, ProtectionState::NORMAL);
}

// ════════════════════════════════════════════════════════════════
// Mismatch detection
// ════════════════════════════════════════════════════════════════

TEST(Reconcile, DetectsPowerMismatch) {
    auto in = baseline();
    in.current_power = HPPower::OFF;
    const auto d = decide_reconciliation(in);
    EXPECT_TRUE(d.mode_mismatch);
    EXPECT_TRUE(d.should_command);
    EXPECT_TRUE(d.target.power_on);
}

TEST(Reconcile, DetectsModeMismatch) {
    auto in = baseline();
    in.current_mode = HPMode::COOL;
    const auto d = decide_reconciliation(in);
    EXPECT_TRUE(d.mode_mismatch);
    EXPECT_EQ(d.target.mode, HPMode::HEAT);
}

TEST(Reconcile, DetectsSetpointMismatch) {
    auto in = baseline();
    in.current_temperature = 23.0f;
    const auto d = decide_reconciliation(in);
    EXPECT_TRUE(d.temp_mismatch);
    EXPECT_FALSE(d.mode_mismatch);
}

TEST(Reconcile, HalfDegreeRoundingIsNotAMismatch) {
    auto in = baseline();
    in.desired_temp = 20.1f;          // normalizes to 20.0
    in.current_temperature = 20.0f;
    const auto d = decide_reconciliation(in);
    EXPECT_FALSE(d.temp_mismatch);
}

TEST(Reconcile, UnknownSetpointCountsAsMismatch) {
    auto in = baseline();
    in.current_temperature = std::nullopt;  // nothing read back yet
    const auto d = decide_reconciliation(in);
    EXPECT_TRUE(d.temp_mismatch);
}

TEST(Reconcile, WantingOffWhileUnitIsOnIsAMismatch) {
    auto in = baseline();
    in.desired_power_on = false;
    in.desired_mode = std::nullopt;
    const auto d = decide_reconciliation(in);
    EXPECT_TRUE(d.mode_mismatch);
    EXPECT_FALSE(d.target.power_on);
    EXPECT_TRUE(d.should_command);
}

TEST(Reconcile, SetpointIsIrrelevantWhenTargetIsOff) {
    auto in = baseline();
    in.desired_power_on = false;
    in.desired_mode = std::nullopt;
    in.current_power = HPPower::OFF;
    in.current_temperature = 30.0f;  // nowhere near desired_temp
    const auto d = decide_reconciliation(in);
    EXPECT_FALSE(d.has_mismatch());
}

TEST(Reconcile, UnmappableDesiredModeLeavesUnitModeAlone) {
    auto in = baseline();
    in.desired_mode = std::nullopt;  // no heatpump equivalent
    in.current_mode = HPMode::COOL;
    const auto d = decide_reconciliation(in);
    EXPECT_FALSE(d.target.mode.has_value());
    EXPECT_FALSE(d.mode_mismatch);  // power already ON, mode not compared
}

// ════════════════════════════════════════════════════════════════
// Low temperature protection latch
// ════════════════════════════════════════════════════════════════

TEST(LtpLatch, EngagesBelowThreshold) {
    EXPECT_TRUE(next_ltp_active(true, false, 7.9f, 8.0f, 4.0f));
    EXPECT_FALSE(next_ltp_active(true, false, 8.0f, 8.0f, 4.0f));
}

TEST(LtpLatch, HoldsInsideHysteresisBand) {
    // Engaged at 7.9; must stay engaged all the way up to the release point.
    EXPECT_TRUE(next_ltp_active(true, true, 8.5f, 8.0f, 4.0f));
    EXPECT_TRUE(next_ltp_active(true, true, 12.0f, 8.0f, 4.0f));
}

TEST(LtpLatch, ReleasesAboveHysteresis) {
    EXPECT_FALSE(next_ltp_active(true, true, 12.1f, 8.0f, 4.0f));
}

TEST(LtpLatch, UnknownTemperatureChangesNothing) {
    EXPECT_TRUE(next_ltp_active(true, true, NAN, 8.0f, 4.0f));
    EXPECT_FALSE(next_ltp_active(true, false, NAN, 8.0f, 4.0f));
}

TEST(LtpLatch, DisablingTheSwitchAlwaysReleases) {
    EXPECT_FALSE(next_ltp_active(false, true, 2.0f, 8.0f, 4.0f));
}

// ════════════════════════════════════════════════════════════════
// Low temperature protection behaviour
// ════════════════════════════════════════════════════════════════

TEST(Reconcile, LtpForcesHeatEvenWhenUserWantsOff) {
    auto in = baseline();
    in.desired_power_on = false;
    in.desired_mode = std::nullopt;
    in.ltp_enabled = true;
    in.current_temp = 5.0f;
    in.current_power = HPPower::OFF;

    const auto d = decide_reconciliation(in);
    EXPECT_TRUE(d.ltp_active);
    EXPECT_TRUE(d.target.power_on);
    EXPECT_EQ(d.target.mode, HPMode::HEAT);
    EXPECT_TRUE(d.should_command);
    EXPECT_EQ(d.protection, ProtectionState::LOW_TEMP_PROTECTION);
}

TEST(Reconcile, LtpAimsAboveTheReleaseThreshold) {
    auto in = baseline();
    in.desired_power_on = false;
    in.desired_mode = std::nullopt;
    in.desired_temp = 10.0f;
    in.ltp_enabled = true;
    in.current_temp = 5.0f;

    const auto d = decide_reconciliation(in);
    EXPECT_FLOAT_EQ(d.target.temperature, 12.0f);  // low_temp_temp + hysteresis
}

TEST(Reconcile, LtpDoesNotFightAWarmerUserSetpoint) {
    auto in = baseline();
    in.desired_temp = 22.0f;
    in.ltp_enabled = true;
    in.current_temp = 5.0f;

    const auto d = decide_reconciliation(in);
    EXPECT_FLOAT_EQ(d.target.temperature, 22.0f);
}

TEST(Reconcile, LtpOutranksFanStop) {
    auto in = baseline();
    in.ltp_enabled = true;
    in.current_temp = 5.0f;
    in.fan_stop_enabled = true;
    in.desired_temp = 20.0f;  // fan stop alone would switch off at 5 C? no — it would demand heat
    const auto d = decide_reconciliation(in);
    EXPECT_TRUE(d.ltp_active);
    EXPECT_EQ(d.target.mode, HPMode::HEAT);
    EXPECT_EQ(d.protection, ProtectionState::LOW_TEMP_PROTECTION);
}

// Regression for the LTP command loop: with the shipped defaults LTP asks for 12 C,
// which is below the 16 C the unit accepts. While the setpoint was clamped to 10-31
// the unit silently raised it to 16, the readback never matched the commanded value,
// and the reconciler re-issued the command every settle window forever.
TEST(Reconcile, LtpTargetConvergesAgainstTheUnitsMinimumSetpoint) {
    auto in = baseline();
    in.desired_power_on = false;
    in.desired_mode = std::nullopt;
    in.desired_temp = 10.0f;
    in.ltp_enabled = true;
    in.ltp_active = true;
    in.current_temp = 9.0f;

    // First pass: unit is off, so we command it.
    in.current_power = HPPower::OFF;
    const auto first = decide_reconciliation(in);
    ASSERT_TRUE(first.should_command);
    EXPECT_FLOAT_EQ(first.target.temperature, 12.0f);

    // The unit accepts the command and reports back the value it actually took,
    // which is its own minimum. That must count as converged.
    in.current_power = HPPower::ON;
    in.current_mode = HPMode::HEAT;
    in.current_temperature = cn105_protocol::SETPOINT_MIN_C;
    const auto second = decide_reconciliation(in);
    EXPECT_FALSE(second.temp_mismatch);
    EXPECT_FALSE(second.has_mismatch());
    EXPECT_FALSE(second.should_command);
}

// ════════════════════════════════════════════════════════════════
// Fan stop
// ════════════════════════════════════════════════════════════════

TEST(FanStopBand, HeatStopsOnceSatisfied) {
    EXPECT_FALSE(fan_stop_wants_running(HPMode::HEAT, 20.5f, 20.0f, 0.5f, HPPower::ON));
}

TEST(FanStopBand, HeatRunsOnceDemandReturns) {
    EXPECT_TRUE(fan_stop_wants_running(HPMode::HEAT, 19.5f, 20.0f, 0.5f, HPPower::OFF));
}

// Inside the deadband the unit holds whatever it is doing — that is what makes it a
// deadband rather than a threshold, and it is why current power is an input.
TEST(FanStopBand, HeatHoldsInsideDeadband) {
    EXPECT_TRUE(fan_stop_wants_running(HPMode::HEAT, 20.0f, 20.0f, 0.5f, HPPower::ON));
    EXPECT_FALSE(fan_stop_wants_running(HPMode::HEAT, 20.0f, 20.0f, 0.5f, HPPower::OFF));
}

TEST(FanStopBand, CoolIsSymmetric) {
    EXPECT_FALSE(fan_stop_wants_running(HPMode::COOL, 19.5f, 20.0f, 0.5f, HPPower::ON));
    EXPECT_TRUE(fan_stop_wants_running(HPMode::COOL, 20.5f, 20.0f, 0.5f, HPPower::OFF));
    EXPECT_TRUE(fan_stop_wants_running(HPMode::COOL, 20.0f, 20.0f, 0.5f, HPPower::ON));
    EXPECT_FALSE(fan_stop_wants_running(HPMode::COOL, 20.0f, 20.0f, 0.5f, HPPower::OFF));
}

TEST(Reconcile, FanStopSwitchesUnitOffWhenSatisfied) {
    auto in = baseline();
    in.fan_stop_enabled = true;
    in.current_temp = 21.0f;  // above 20.0 + 0.5
    const auto d = decide_reconciliation(in);
    EXPECT_FALSE(d.target.power_on);
    EXPECT_TRUE(d.mode_mismatch);
    EXPECT_TRUE(d.should_command);
}

TEST(Reconcile, FanStopLeavesUnitRunningWhenDemandRemains) {
    auto in = baseline();
    in.fan_stop_enabled = true;
    in.current_temp = 19.0f;
    const auto d = decide_reconciliation(in);
    EXPECT_TRUE(d.target.power_on);
    EXPECT_FALSE(d.has_mismatch());
}

TEST(Reconcile, FanStopIgnoresModesWithNoSatisfiedNotion) {
    for (HPMode mode : {HPMode::DRY, HPMode::FAN}) {
        auto in = baseline();
        in.fan_stop_enabled = true;
        in.desired_mode = mode;
        in.current_mode = mode;
        in.current_temp = 30.0f;  // way past any threshold
        const auto d = decide_reconciliation(in);
        EXPECT_TRUE(d.target.power_on) << "mode " << static_cast<int>(mode);
    }
}

TEST(Reconcile, FanStopNeedsBothTemperaturesToBeKnown) {
    auto in = baseline();
    in.fan_stop_enabled = true;
    in.current_temp = NAN;
    EXPECT_TRUE(decide_reconciliation(in).target.power_on);

    in = baseline();
    in.fan_stop_enabled = true;
    in.current_temp = 30.0f;
    in.desired_temp = NAN;
    EXPECT_TRUE(decide_reconciliation(in).target.power_on);
}

TEST(Reconcile, ReportsFanStopActiveInDiagnostic) {
    auto in = baseline();
    in.fan_stop_enabled = true;
    in.current_power = HPPower::OFF;   // unit is off ...
    in.desired_power_on = true;        // ... but the user still wants heat
    in.current_temp = 21.0f;
    const auto d = decide_reconciliation(in);
    EXPECT_EQ(d.protection, ProtectionState::FAN_STOP_ACTIVE);
}

TEST(Reconcile, DiagnosticIsNormalWhenUserAskedForOff) {
    auto in = baseline();
    in.fan_stop_enabled = true;
    in.desired_power_on = false;
    in.desired_mode = std::nullopt;
    in.current_power = HPPower::OFF;
    const auto d = decide_reconciliation(in);
    EXPECT_EQ(d.protection, ProtectionState::NORMAL);
}

// ════════════════════════════════════════════════════════════════
// Repeat suppression
// ════════════════════════════════════════════════════════════════

TEST(Reconcile, SuppressesRepeatOfAnAlreadyCommandedTarget) {
    auto in = baseline();
    in.current_power = HPPower::OFF;  // mismatch persists: no cycle has confirmed yet
    in.has_issued_command = true;
    in.last_issued = PhysicalTarget{true, HPMode::HEAT, 20.0f};
    in.last_issued_ms = in.now_ms - 100;

    const auto d = decide_reconciliation(in);
    EXPECT_TRUE(d.has_mismatch());
    EXPECT_TRUE(d.suppressed_repeat);
    EXPECT_FALSE(d.should_command);
}

TEST(Reconcile, CommandsAgainOnceTheSettleWindowExpires) {
    auto in = baseline();
    in.current_power = HPPower::OFF;
    in.has_issued_command = true;
    in.last_issued = PhysicalTarget{true, HPMode::HEAT, 20.0f};
    in.last_issued_ms = in.now_ms - command_settle_window_ms(in.update_interval_ms) - 1;

    const auto d = decide_reconciliation(in);
    EXPECT_FALSE(d.suppressed_repeat);
    EXPECT_TRUE(d.should_command);
}

// A genuinely new target must go out immediately, however recently we commanded.
TEST(Reconcile, DoesNotSuppressADifferentTarget) {
    auto in = baseline();
    in.desired_temp = 24.0f;
    in.has_issued_command = true;
    in.last_issued = PhysicalTarget{true, HPMode::HEAT, 20.0f};
    in.last_issued_ms = in.now_ms - 10;

    const auto d = decide_reconciliation(in);
    EXPECT_TRUE(d.temp_mismatch);
    EXPECT_FALSE(d.suppressed_repeat);
    EXPECT_TRUE(d.should_command);
}

TEST(Reconcile, DoesNotSuppressWhenNothingWasEverCommanded) {
    auto in = baseline();
    in.current_power = HPPower::OFF;
    in.has_issued_command = false;
    const auto d = decide_reconciliation(in);
    EXPECT_FALSE(d.suppressed_repeat);
    EXPECT_TRUE(d.should_command);
}

// ════════════════════════════════════════════════════════════════
// PhysicalTarget::same_as
// ════════════════════════════════════════════════════════════════

TEST(PhysicalTargetSameAs, OffMatchesOffRegardlessOfSetpoint) {
    const PhysicalTarget a{false, std::nullopt, 20.0f};
    const PhysicalTarget b{false, HPMode::HEAT, 31.0f};
    EXPECT_TRUE(a.same_as(b));
}

TEST(PhysicalTargetSameAs, OffNeverMatchesOn) {
    const PhysicalTarget off{false, std::nullopt, NAN};
    const PhysicalTarget on{true, HPMode::HEAT, 20.0f};
    EXPECT_FALSE(off.same_as(on));
    EXPECT_FALSE(on.same_as(off));
}

TEST(PhysicalTargetSameAs, ModeMustAgreeWhenRunning) {
    const PhysicalTarget heat{true, HPMode::HEAT, 20.0f};
    const PhysicalTarget cool{true, HPMode::COOL, 20.0f};
    EXPECT_FALSE(heat.same_as(cool));
}

TEST(PhysicalTargetSameAs, SetpointsWithinAQuarterDegreeAreTheSame) {
    const PhysicalTarget a{true, HPMode::HEAT, 20.0f};
    const PhysicalTarget b{true, HPMode::HEAT, 20.2f};
    const PhysicalTarget c{true, HPMode::HEAT, 20.3f};
    EXPECT_TRUE(a.same_as(b));
    EXPECT_FALSE(a.same_as(c));
}

// An unknown setpoint cannot be shown equal to anything, so it must not suppress.
TEST(PhysicalTargetSameAs, NanSetpointNeverMatches) {
    const PhysicalTarget a{true, HPMode::HEAT, NAN};
    const PhysicalTarget b{true, HPMode::HEAT, 20.0f};
    EXPECT_FALSE(a.same_as(b));
    EXPECT_FALSE(b.same_as(a));
    EXPECT_FALSE(a.same_as(a));
}

// ════════════════════════════════════════════════════════════════
// Scenario: the reconciler must settle rather than oscillate
// ════════════════════════════════════════════════════════════════

// Drive a full fan-stop cycle the way loop() would, feeding each decision back in
// as the unit's new state, and check it reaches a steady state instead of
// commanding on every pass.
TEST(ReconcileScenario, FanStopCycleSettlesInsteadOfOscillating) {
    auto in = baseline();
    in.fan_stop_enabled = true;
    in.desired_temp = 20.0f;

    int commands = 0;
    // Room warms past the satisfied point, then holds there.
    const float room_readings[] = {19.0f, 19.8f, 20.4f, 20.6f, 20.8f, 20.7f, 20.6f, 20.6f};

    for (float room : room_readings) {
        in.current_temp = room;
        const auto d = decide_reconciliation(in);
        if (d.should_command) {
            commands++;
            // The unit obeys before the next evaluation.
            in.current_power = d.target.power_on ? HPPower::ON : HPPower::OFF;
            if (d.target.power_on && d.target.mode.has_value()) {
                in.current_mode = *d.target.mode;
            }
            in.current_temperature = cn105_protocol::normalize_setpoint(d.target.temperature);
            in.has_issued_command = true;
            in.last_issued = d.target;
            in.last_issued_ms = in.now_ms;
        }
        in.now_ms += 5000;  // loop() re-evaluates every 5 s
    }

    // Exactly one transition: running -> off when the room passed 20.5.
    EXPECT_EQ(commands, 1);
    EXPECT_EQ(in.current_power, HPPower::OFF);
}

// The same guard from the other direction: an unchanging mismatch the unit refuses
// to accept must be re-tried on the settle window, not on every evaluation.
TEST(ReconcileScenario, UnacceptedCommandIsRetriedOnTheSettleWindowOnly) {
    auto in = baseline();
    in.current_power = HPPower::OFF;  // unit never obeys

    int commands = 0;
    const uint32_t settle = command_settle_window_ms(in.update_interval_ms);
    for (int i = 0; i < 20; i++) {
        const auto d = decide_reconciliation(in);
        if (d.should_command) {
            commands++;
            in.has_issued_command = true;
            in.last_issued = d.target;
            in.last_issued_ms = in.now_ms;
        }
        in.now_ms += 5000;
    }

    // 20 evaluations over 100 s, with a 7.5 s settle window: far fewer than 20.
    EXPECT_GT(commands, 1);
    EXPECT_LE(commands, static_cast<int>(20 * 5000 / settle) + 1);
}
