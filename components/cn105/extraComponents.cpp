#include "cn105.h"

using namespace esphome;


class VaneOrientationSelect : public select::Select {
public:
    VaneOrientationSelect(CN105Climate* parent) : parent_(parent) {}

    void control(const std::string& value) override {

        ESP_LOGD("EVT", "vane.control() -> Demande un chgt de réglage de la vane: %s", value.c_str());

        parent_->setVaneSetting(value.c_str()); // should be enough to trigger a sendWantedSettings
        parent_->wantedSettings.hasChanged = true;
        parent_->wantedSettings.hasBeenSent = false;
        // now updated thanks to new sendWantedSettings policy 
        // parent_->sendWantedSettings();

    }
private:
    CN105Climate* parent_;

};

void CN105Climate::generateExtraComponents() {
    this->compressor_frequency_sensor = new sensor::Sensor();
    this->compressor_frequency_sensor->set_name("Compressor Frequency");
    this->compressor_frequency_sensor->set_unit_of_measurement("Hz");
    this->compressor_frequency_sensor->set_accuracy_decimals(0);
    this->compressor_frequency_sensor->publish_state(0);

    App.register_sensor(compressor_frequency_sensor);

    this->iSee_sensor = new binary_sensor::BinarySensor();
    this->iSee_sensor->set_name("iSee sensor");
    this->iSee_sensor->publish_initial_state(false);
    App.register_binary_sensor(this->iSee_sensor);

    this->vane = new VaneOrientationSelect(this);
    this->vane->set_name("Vane");

    std::vector<std::string> vaneOptions(std::begin(VANE_MAP), std::end(VANE_MAP));
    this->vane->traits.set_options(vaneOptions);

    App.register_select(this->vane);
}