#include "cn105.h"
#include <algorithm>
#include <esphome/core/helpers.h>

using namespace esphome;

void CN105Climate::generate_extra_components() {
  /*this->iSee_sensor = new binary_sensor::BinarySensor();
  this->iSee_sensor->set_name("i_see sensor");
  this->iSee_sensor->publish_initial_state(false);
  App.register_binary_sensor(this->iSee_sensor);*/
}

void CN105Climate::set_vertical_vane_select(VaneOrientationSelect *vertical_vane_select) {
  this->vertical_vane_select_ = vertical_vane_select;

  // builds option list from VANE_TABLE labels
  this->vertical_vane_select_->traits.set_options({VANE_TABLE[0].label, VANE_TABLE[1].label, VANE_TABLE[2].label,
                                                   VANE_TABLE[3].label, VANE_TABLE[4].label, VANE_TABLE[5].label,
                                                   VANE_TABLE[6].label});

  this->vertical_vane_select_->set_callback_function([this](const char *setting) {
    ESP_LOGD("EVT", "vane.control() -> Requesting change in vane setting: %s", setting);

    this->set_vane_setting(setting);
    this->wanted_settings_.has_changed = true;
    this->wanted_settings_.has_been_sent = false;
    this->wanted_settings_.last_change = CUSTOM_MILLIS;
  });
}

void CN105Climate::set_horizontal_vane_select(VaneOrientationSelect *horizontal_vane_select,
                                              const std::vector<std::string> &options) {
  this->horizontal_vane_select_ = horizontal_vane_select;

  // Use provided options if not empty, and filter out any options that are not in WIDEVANE_TABLE to ensure validity,
  // otherwise use all options from WIDEVANE_TABLE
  if (!options.empty()) {
    this->horizontal_vane_options_strings_.clear();
    for (const auto &option : options) {
      bool found = false;
      for (const auto &entry : WIDEVANE_TABLE) {
        if (option == entry.label) {
          found = true;
          break;
        }
      }
      if (found) {
        this->horizontal_vane_options_strings_.push_back(option);
      }
    }
  } else {
    this->horizontal_vane_options_strings_.clear();
    for (const auto &entry : WIDEVANE_TABLE) {
      this->horizontal_vane_options_strings_.push_back(entry.label);
    }
  }

  // Build FixedVector of const char* for set_options
  FixedVector<const char *> fixed_options;
  fixed_options.init(this->horizontal_vane_options_strings_.size());
  for (const auto &str : this->horizontal_vane_options_strings_) {
    fixed_options.push_back(str.c_str());
  }
  this->horizontal_vane_select_->traits.set_options(fixed_options);

  this->horizontal_vane_select_->set_callback_function([this](const char *setting) {
    ESP_LOGD("EVT", "wide_vane.control() -> Requesting change in wide_vane setting: %s", setting);

    this->set_wide_vane_setting(setting);
    this->wanted_settings_.has_changed = true;
    this->wanted_settings_.has_been_sent = false;
    this->wanted_settings_.last_change = CUSTOM_MILLIS;
  });
}

void CN105Climate::set_airflow_control_select(VaneOrientationSelect *airflow_control_select) {
  this->airflow_control_select_ = airflow_control_select;

  this->airflow_control_select_->traits.set_options(
      {AIRFLOW_CONTROL_TABLE[0].label, AIRFLOW_CONTROL_TABLE[1].label, AIRFLOW_CONTROL_TABLE[2].label});

  this->airflow_control_select_->set_callback_function([this](const char *setting) {
    if (this->current_settings_.wide_vane == HPWideVaneMode::AIRFLOW_CONTROL) {
      ESP_LOGD("EVT", "airFlow -> Request for change of airflow control setting: %s", setting);

      this->set_airflow_control_setting(setting);
      this->wanted_run_states_.has_changed = true;
      this->wanted_run_states_.has_been_sent = false;
      this->wanted_run_states_.last_change = CUSTOM_MILLIS;
    } else {
      this->airflow_control_select_->publish_state(hp_airflow_control_to_str(this->current_run_states_.airflow_control));
    }
  });
}

void CN105Climate::set_compressor_frequency_sensor(sensor::Sensor *compressor_frequency_sensor) {
  this->compressor_frequency_sensor_ = compressor_frequency_sensor;
}

void CN105Climate::set_target_humidity_sensor(sensor::Sensor *target_humidity_sensor) {
  this->target_humidity_sensor_ = target_humidity_sensor;
}

void CN105Climate::set_input_power_sensor(sensor::Sensor *input_power_sensor) {
  this->input_power_sensor_ = input_power_sensor;
}

void CN105Climate::set_kwh_sensor(sensor::Sensor *kwh_sensor) { this->kwh_sensor_ = kwh_sensor; }

void CN105Climate::set_runtime_hours_sensor(sensor::Sensor *runtime_hours_sensor) {
  this->runtime_hours_sensor_ = runtime_hours_sensor;
}

void CN105Climate::set_outside_air_temperature_sensor(sensor::Sensor *outside_air_temperature_sensor) {
  this->outside_air_temperature_sensor_ = outside_air_temperature_sensor;
}

void CN105Climate::set_isee_sensor(esphome::binary_sensor::BinarySensor *iSee_sensor) {
  this->isee_sensor_ = iSee_sensor;
}

void CN105Climate::set_stage_sensor(esphome::text_sensor::TextSensor *stage_sensor) {
  this->stage_sensor_ = stage_sensor;
}
void CN105Climate::set_use_stage_for_operating_status(bool value) {
  this->use_stage_for_operating_status_ = value;
  ESP_LOGI(TAG, "Using stage sensor as operating fallback: %s", value ? "true" : "false");
}

void CN105Climate::set_functions_sensor(esphome::text_sensor::TextSensor *Functions_sensor) {
  this->functions_sensor_ = Functions_sensor;
}

void CN105Climate::set_functions_get_button(FunctionsButton *Button) {
  this->functions_get_button_ = Button;
  this->functions_get_button_->set_callback_function([this]() {
    ESP_LOGI(LOG_CYCLE_TAG, "Retrieving functions");

    if (this->functions_sensor_ != nullptr) {
      this->functions_sensor_->publish_state("Operation pending, please wait.");
    }

    // Request function settings from the heat pump.
    this->is_get_functions_ = true;

    // The response is handled in heatpump_functions.cpp
  });
}

void CN105Climate::set_functions_set_button(FunctionsButton *Button) {
  this->functions_set_button_ = Button;
  this->functions_set_button_->set_callback_function([this]() {
    if (!this->functions.is_valid()) {
      if (this->functions_sensor_ != nullptr) {
        this->functions_sensor_->publish_state("Please get the functions first.");
      }
      return;
    }

    ESP_LOGI(LOG_CYCLE_TAG, "Setting code %i to value %i", this->functions_code_, this->functions_value_);
    this->functions.set_value(this->functions_code_, this->functions_value_);

    if (this->functions_sensor_ != nullptr) {
      this->functions_sensor_->publish_state("Operation pending, please wait.");
    }

    // Now send the codes.
    this->is_set_functions_ = true;
  });
}

void CN105Climate::set_functions_set_code(FunctionsNumber *Number) {
  this->functions_set_code_ = Number;
  this->functions_set_code_->set_callback_function([this](float x) {
    // store the code
    this->functions_code_ = (int) x;
  });
}
void CN105Climate::set_functions_set_value(FunctionsNumber *Number) {
  this->functions_set_value_ = Number;
  this->functions_set_value_->set_callback_function([this](float x) {
    // store the value
    this->functions_value_ = (int) x;
  });
}

void CN105Climate::set_air_purifier_switch(HVACOptionSwitch *Switch) {
  this->air_purifier_switch_ = Switch;
  this->air_purifier_switch_->set_callback_function([this](bool state) {
    this->wanted_run_states_.air_purifier = state;

    this->wanted_run_states_.has_changed = true;
    this->wanted_run_states_.has_been_sent = false;
    this->wanted_run_states_.last_change = CUSTOM_MILLIS;
  });
}

void CN105Climate::set_night_mode_switch(HVACOptionSwitch *Switch) {
  this->night_mode_switch_ = Switch;
  this->night_mode_switch_->set_callback_function([this](bool state) {
    this->wanted_run_states_.night_mode = state;

    this->wanted_run_states_.has_changed = true;
    this->wanted_run_states_.has_been_sent = false;
    this->wanted_run_states_.last_change = CUSTOM_MILLIS;
  });
}

void CN105Climate::set_circulator_switch(
    HVACOptionSwitch
        *Switch) {  // only in HEAT mode? Manual says so, but it is possible to set the bit. The remote will not do it.
  this->circulator_switch_ = Switch;
  this->circulator_switch_->set_callback_function([this](bool state) {
    this->wanted_run_states_.circulator = state;

    this->wanted_run_states_.has_changed = true;
    this->wanted_run_states_.has_been_sent = false;
    this->wanted_run_states_.last_change = CUSTOM_MILLIS;
  });
}

void CN105Climate::set_sub_mode_sensor(esphome::text_sensor::TextSensor *Sub_mode_sensor) {
  this->sub_mode_sensor_ = Sub_mode_sensor;
}

void CN105Climate::set_auto_sub_mode_sensor(esphome::text_sensor::TextSensor *Auto_sub_mode_sensor) {
  this->auto_sub_mode_sensor_ = Auto_sub_mode_sensor;
}

void CN105Climate::set_error_code_sensor(esphome::text_sensor::TextSensor *error_code_sensor) {
  this->error_code_sensor_ = error_code_sensor;
}

void CN105Climate::set_remote_temp_source(esphome::sensor::Sensor *source) {
  this->remote_temp_source_ = source;
  // Subscribe to source sensor state changes and auto-feed remote temperature
  source->add_on_state_callback([this](float value) { this->set_remote_temperature(value); });
}

void CN105Climate::set_remote_temp_source_info_sensor(esphome::text_sensor::TextSensor *info_sensor) {
  this->remote_temp_source_info_sensor_ = info_sensor;
  // Publish the source sensor name on next loop
  if (this->remote_temp_source_ != nullptr) {
    info_sensor->publish_state(this->remote_temp_source_->get_name());
  }
}

void CN105Climate::set_hp_uptime_connection_sensor(cn105::HpUpTimeConnectionSensor *hp_up_connection_sensor) {
  this->hp_uptime_connection_sensor_ = hp_up_connection_sensor;
}

void CN105Climate::add_hardware_setting(HardwareSettingSelect *setting) {
  this->hardware_settings_.push_back(setting);
  setting->set_callback_function([this, setting](const std::string &value, int int_value) {
    ESP_LOGI(LOG_FUNCTIONS_TAG, "Hardware setting change: Code %d -> %d (%s)", setting->get_code(), int_value,
             value.c_str());

    // Optimistic update done in component

    // Update internal structure
    this->functions.set_value(setting->get_code(), int_value);

    // Trigger write to device
    this->is_set_functions_ = true;
  });
}
