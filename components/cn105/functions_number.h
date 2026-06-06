#pragma once

#include "esphome/components/number/number.h"
#include "esphome/core/component.h"

namespace esphome {

class FunctionsNumber : public number::Number, public Component {
 public:
  using CallbackFunction = std::function<void(float number)>;
  FunctionsNumber() {}

  // This callback function links the number changes back to the Climate component
  void set_callback_function(CallbackFunction &&callback) { this->callback_function_ = std::move(callback); }

 protected:
  void control(float x) override {  // called when the number changes
    if (callback_function_) {
      callback_function_(x);  // Trigger the callback function
    }
  }

 private:
  CallbackFunction callback_function_;
};
}  // namespace esphome