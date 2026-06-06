/// esphome_stubs.h — Minimal stubs to compile the CN105 functions outside ESPHome.
/// Deps: <cstdio>, <cstdint>, <cstring>, <cmath>, <string>, <functional>
#pragma once

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <functional>

// Stub out the ESPHome logging macros → no-op in test mode
#define ESP_LOGD(tag, fmt, ...) ((void)0)
#define ESP_LOGI(tag, fmt, ...) ((void)0)
#define ESP_LOGW(tag, fmt, ...) ((void)0)
#define ESP_LOGE(tag, fmt, ...) ((void)0)
#define ESP_LOGV(tag, fmt, ...) ((void)0)
