#pragma once

#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"

namespace esphome {

class HVACOptionSwitch : public switch_::Switch, public Component {
 public:
  using CallbackFunction = std::function<void(bool state)>;

  // HVACOptionSwitch() {}

  // This callback function links the button press to the Climate component
  void set_callback_function(CallbackFunction &&callback) { this->callback_function_ = std::move(callback); }

 protected:
  void write_state(bool state) override {
    if (callback_function_) {
      callback_function_(state);  // Trigger the callback function
    }
  }

 private:
  CallbackFunction callback_function_;
};

}  // namespace esphome