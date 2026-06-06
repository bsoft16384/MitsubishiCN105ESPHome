#pragma once

#include "esphome/components/button/button.h"
#include "esphome/core/component.h"

namespace esphome {

class FunctionsButton : public button::Button, public Component {
 public:
  using CallbackFunction = std::function<void()>;

  FunctionsButton() {}

  // This callback function links the button press to the Climate component
  void set_callback_function(CallbackFunction &&callback) { this->callback_function_ = std::move(callback); }

 protected:
  void press_action() override {
    if (callback_function_) {
      callback_function_();  // Trigger the callback function
    }
  }

 private:
  CallbackFunction callback_function_;
};

}  // namespace esphome