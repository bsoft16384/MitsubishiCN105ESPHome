#include "cycle_management.h"
#include "cn105.h"
#include "globals.h"

using namespace esphome;

void CycleManagement::check_timeout(unsigned int update_interval) {
  if (does_cycle_time_out(update_interval)) {  // does it last too long ?
    ESP_LOGW(TAG, "Cycle timeout, resetting cycle...");
    cycle_ended(true);
  }
}

bool CycleManagement::is_cycle_running() { return cycle_running; }

void CycleManagement::init() {
  cycle_running = false;
  last_complete_cycle_ms = CUSTOM_MILLIS;
  last_cycle_end_ms = CUSTOM_MILLIS;
}

void CycleManagement::defer_cycle() {
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_DEBUG
  uint32_t delay = DEFER_SCHEDULE_UPDATE_LOOP_DELAY * 2;
#else
  uint32_t delay = DEFER_SCHEDULE_UPDATE_LOOP_DELAY;
#endif

  log_info_uint32(LOG_CYCLE_TAG, "Deferring cycle trigger by ", delay, " ms");
  // forces the lastCompleteCycle offset of delay ms to allow a longer rest time
  last_complete_cycle_ms = CUSTOM_MILLIS + delay;
}
void CycleManagement::cycle_started() {
  ESP_LOGI(LOG_CYCLE_TAG, "1: Cycle start");
  last_cycle_start_ms = CUSTOM_MILLIS;
  cycle_running = true;
}

void CycleManagement::cycle_ended(bool timed_out) {
  cycle_running = false;

  // Unconditional: this is the starvation detector's reference point, so it must
  // not inherit the defer offset applied to last_complete_cycle_ms below.
  last_cycle_end_ms = CUSTOM_MILLIS;

  if (last_complete_cycle_ms < CUSTOM_MILLIS) {  // we check this because of the deferring mechanism
    // a complete cycle is done
    last_complete_cycle_ms = CUSTOM_MILLIS;  // to prevent next inteval from ticking too soon
  }

  ESP_LOGI(LOG_CYCLE_TAG, "6: Cycle ended in %.1f seconds (with timeout?: %s)",
           (last_complete_cycle_ms - last_cycle_start_ms) / 1000.0, timed_out ? "YES" : " NO");
}

bool CycleManagement::has_update_interval_passed(unsigned int update_interval) {
  if (CUSTOM_MILLIS < last_complete_cycle_ms)
    return false;  // must be checked because operands are they are unsigned
  return (CUSTOM_MILLIS - last_complete_cycle_ms) > update_interval;
}

bool CycleManagement::does_cycle_time_out(unsigned int update_interval) {
  if (CUSTOM_MILLIS < last_cycle_start_ms)
    return false;  // must be checked because operands are they are unsigned
  return (CUSTOM_MILLIS - last_cycle_start_ms) > (2 * update_interval) + 1000;
}
