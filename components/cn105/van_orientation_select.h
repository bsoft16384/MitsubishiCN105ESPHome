#pragma once

#include "esphome/components/select/select.h"
#include "esphome/core/component.h"

namespace esphome {

class VaneOrientationSelect : public select::Select, public Component {
 public:
  using CallbackFunction = std::function<void(const char *setting)>;

  void set_callback_function(CallbackFunction &&callback) { this->callback_function_ = std::move(callback); }

 protected:
  void control(const std::string &value) override {
    if (callback_function_) {
      callback_function_(value.c_str());  // should be enough to trigger a send_wanted_settings
    }
  }

 private:
  CallbackFunction callback_function_;
};

}  // namespace esphome