#include "hardware_setting_select.h"
#include "globals.h"
#include "esphome/core/log.h"

namespace esphome {

HardwareSettingSelect::HardwareSettingSelect(int code, const std::map<int, std::string> &options)
    : code_(code), mapping_(options) {
  for (auto const &[val, label] : options) {
    reverse_mapping_[label] = val;
  }
}

void HardwareSettingSelect::set_callback_function(CallbackFunction &&callback) { this->callback_ = std::move(callback); }

int HardwareSettingSelect::get_code() const { return code_; }

void HardwareSettingSelect::update_state_from_value(int value) {
  ESP_LOGD(LOG_HARDWARE_SELECT_TAG, "Code %d update request with value: %d", this->code_, value);

  auto it = this->mapping_.find(value);
  if (it == this->mapping_.end()) {
    ESP_LOGW(LOG_HARDWARE_SELECT_TAG, "Code %d received unknown value: %d", this->code_, value);
    return;
  }

  const std::string &new_state = it->second;

  // current_option() yields an empty StringRef (not a null pointer) before the first publish.
  const StringRef cur = this->current_option();
  const bool changed = cur.empty() || std::strcmp(cur.c_str(), new_state.c_str()) != 0;

  if (changed) {
    ESP_LOGI(LOG_HARDWARE_SELECT_TAG, "Code %d state changed: %s -> %s (val: %d)", this->code_,
             cur.empty() ? "<none>" : cur.c_str(), new_state.c_str(), value);
    this->publish_state(new_state);
  } else {
    ESP_LOGD(LOG_HARDWARE_SELECT_TAG, "Code %d state unchanged: %s (val: %d)", this->code_, new_state.c_str(), value);
  }
}

void HardwareSettingSelect::control(const std::string &value) {
  if (!this->enabled_ || this->is_failed()) {
    ESP_LOGW(LOG_HARDWARE_SELECT_TAG, "Setting %d is disabled/failed, control ignored.", this->code_);
    return;
  }

  ESP_LOGD(LOG_HARDWARE_SELECT_TAG, "Code %d control request: %s", this->code_, value.c_str());
  if (this->reverse_mapping_.count(value)) {
    int int_value = this->reverse_mapping_[value];
    if (callback_) {
      ESP_LOGD(LOG_HARDWARE_SELECT_TAG, "Code %d calling callback with val: %d", this->code_, int_value);
      callback_(value, int_value);
    } else {
      ESP_LOGW(LOG_HARDWARE_SELECT_TAG, "Code %d no callback registered!", this->code_);
    }
  } else {
    ESP_LOGW(LOG_HARDWARE_SELECT_TAG, "Code %d control value not found in mapping: %s", this->code_, value.c_str());
  }
}

void HardwareSettingSelect::set_enabled(bool enabled) { this->enabled_ = enabled; }
}  // namespace esphome
