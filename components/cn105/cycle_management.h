#pragma once

struct CycleManagement {
  bool cycle_running = false;
  unsigned long last_cycle_start_ms = 0;
  /// Cycle eligibility clock. defer_cycle() may push it into the future, so it
  /// does not answer "when did a cycle last complete?".
  unsigned long last_complete_cycle_ms = 0;
  /// Timestamp of the last actual cycle end, never moved by defer_cycle().
  /// Used to detect cycles being starved by a stream of writes.
  unsigned long last_cycle_end_ms = 0;

  void init();
  void cycle_started();
  void cycle_ended(bool timed_out = false);
  bool has_update_interval_passed(unsigned int update_interval);
  bool does_cycle_time_out(unsigned int update_interval);
  bool is_cycle_running();
  void defer_cycle();
  void check_timeout(unsigned int update_interval);
};