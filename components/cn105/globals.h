#pragma once
#include "cn105_types.h"

// Precise ESPHome core headers (replacing the legacy <esphome.h> umbrella):
//   component.h — Component base + scheduler (set_timeout/set_interval/cancel_*)
//   hal.h       — millis()/delay(), used by CUSTOM_MILLIS
//   log.h       — ESP_LOGx logging macros
//   helpers.h   — assorted helpers (FixedVector, optional, etc.)
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/components/uart/uart.h"

#define CUSTOM_MILLIS esphome::millis()
