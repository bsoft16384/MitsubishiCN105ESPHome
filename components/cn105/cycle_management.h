#pragma once

struct cycleManagement {
  bool cycleRunning = false;
  unsigned long lastCycleStartMs = 0;
  unsigned long lastCompleteCycleMs = 0;

  void init();
  void cycle_started();
  void cycle_ended(bool timed_out = false);
  bool has_update_interval_passed(unsigned int update_interval);
  bool does_cycle_time_out(unsigned int update_interval);
  bool is_cycle_running();
  void defer_cycle();
  void check_timeout(unsigned int update_interval);
};