#include "cn105.h"
#include "heatpumpFunctions.h"
#include "Globals.h"

using namespace esphome;
//#region heatpump_functions fonctions clim

void CN105Climate::functions_arrived() {

    // Called after 2nd packet has arrived.

    char states[256];
    states[0] = '\0';  // Initialize as empty string
    size_t remaining = sizeof(states);
    char* pos = states;

    heatpumpFunctionCodes codes = functions.get_all_codes();
    for (int i = 0; i < MAX_FUNCTION_CODE_COUNT; ++i) {
        if (codes.valid[i]) {
            int code = codes.code[i];
            int value = functions.get_value(code);
            if (value > 0) {  // only values 1, 2, 3 are valid -- 0 values mean something the device does not support
                int written = snprintf(pos, remaining, "%i: %i ", code, value);
                if (written < 0 || static_cast<size_t>(written) >= remaining) {
                    // Buffer full or error
                    break;
                }
                pos += written;
                remaining -= written;
            }
        }
    }

    // Publish the results of all the codes in the Functions sensor
    if (this->functions_sensor_ != nullptr) {
        this->functions_sensor_->publish_state(states);
    }

    // Update Hardware Settings Selects
    for (auto* setting : this->hardware_settings_) {
        int val = functions.get_value(setting->get_code());
        if (val > 0) {
            setting->update_state_from_value(val);
        } else {
            ESP_LOGD(LOG_HARDWARE_SELECT_TAG, "Code %d received unknown value: %d", setting->get_code(), val);
        }
    }
}

bool CN105Climate::set_functions(HeatpumpFunctions const& functions) {
    if (!functions.is_valid()) {
        return false;
    }

    uint8_t packet1[PACKET_LEN] = {};
    uint8_t packet2[PACKET_LEN] = {};

    prepare_set_packet(packet1, PACKET_LEN);
    packet1[5] = FUNCTIONS_SET_PART1;

    prepare_set_packet(packet2, PACKET_LEN);
    packet2[5] = FUNCTIONS_SET_PART2;

    functions.get_data1(&packet1[6]);
    functions.get_data2(&packet2[6]);

    // sanity check, we expect data byte 15 (index 20) to be 0
    // REMOVED for Bug #485 - newer units use these bytes
    // if (packet1[20] != 0 || packet2[20] != 0)
    //    return false;

    // make sure all the other data bytes are set
    // REMOVED for Bug #485
    /* for (int i = 6; i < 20; ++i) {
        if (packet1[i] == 0 || packet2[i] == 0)
            return false;
    } */

    packet1[21] = check_sum(packet1, 21);
    packet2[21] = check_sum(packet2, 21);
    /*
        while (!canSend(false)) {
            //esphome::CUSTOM_DELAY(10);
            CUSTOM_DELAY(10);
        }*/
    ESP_LOGD(TAG, "sending a set_functions packet part 1");
    write_packet(packet1, PACKET_LEN);
    //readPacket();

    /*while (!canSend(false)) {
        //esphome::CUSTOM_DELAY(10);
        CUSTOM_DELAY(10);
    }*/
    ESP_LOGD(TAG, "sending a set_functions packet part 2");
    write_packet(packet2, PACKET_LEN);
    //readPacket();

    return true;
}


HeatpumpFunctions::HeatpumpFunctions() {
    clear();
}

bool HeatpumpFunctions::is_valid() const {
    return _isValid1 && _isValid2;
}

void HeatpumpFunctions::set_data1(uint8_t* data) {
    memcpy(raw, data, 15);
    _isValid1 = true;
}

void HeatpumpFunctions::set_data2(uint8_t* data) {
    memcpy(raw + 15, data, 15);
    _isValid2 = true;
}

void HeatpumpFunctions::get_data1(uint8_t* data) const {
    memcpy(data, raw, 15);
}

void HeatpumpFunctions::get_data2(uint8_t* data) const {
    memcpy(data, raw + 15, 15);
}

void HeatpumpFunctions::clear() {
    memset(raw, 0, sizeof(raw));
    _isValid1 = false;
    _isValid2 = false;
}

int HeatpumpFunctions::get_code(uint8_t b) {
    return ((b >> 2) & 0xff) + 100;
}

int HeatpumpFunctions::get_value(uint8_t b) {
    return b & 3;
}

int HeatpumpFunctions::get_value(int code) {
    if (code > 128 || code < 101)
        return 0;

    for (int i = 0; i < MAX_FUNCTION_CODE_COUNT; ++i) {
        if (get_code(raw[i]) == code)
            return get_value(raw[i]);
    }

    return 0;
}

bool HeatpumpFunctions::set_value(int code, int value) {
    if (code > 128 || code < 101)
        return false;

    if (value < 1 || value > 3)
        return false;

    for (int i = 0; i < MAX_FUNCTION_CODE_COUNT; ++i) {
        if (get_code(raw[i]) == code) {
            raw[i] = ((code - 100) << 2) + value;
            return true;
        }
    }

    return false;
}

heatpumpFunctionCodes HeatpumpFunctions::get_all_codes() {
    heatpumpFunctionCodes result;
    for (int i = 0; i < MAX_FUNCTION_CODE_COUNT; ++i) {
        int code = get_code(raw[i]);
        result.code[i] = code;
        result.valid[i] = (code >= 101 && code <= 128);
    }

    return result;
}

bool HeatpumpFunctions::operator==(const HeatpumpFunctions& rhs) {
    return this->is_valid() == rhs.is_valid() && memcmp(this->raw, rhs.raw, sizeof(this->raw)) == 0;
}

bool HeatpumpFunctions::operator!=(const HeatpumpFunctions& rhs) {
    return !(*this == rhs);
}
//#endregion heatpump_functions
