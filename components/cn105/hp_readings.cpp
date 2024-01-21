#include "cn105.h"

/**
 * Seek the byte pointer to the beginning of the array
 * Initializes few variables
*/
void CN105Climate::initBytePointer() {
    this->foundStart = false;
    this->bytesRead = 0;
    this->dataLength = -1;
    this->command = 0;
}

/**
 *
 * La taille totale d'une trame, se compose de plusieurs éléments :
 * Taille du Header : Le header a une longueur fixe de 5 octets (INFOHEADER_LEN).
 * Longueur des Données : La longueur des données est variable et est spécifiée par le quatrième octet du header (header[4]).
 * Checksum : Il y a 1 octet de checksum à la fin de la trame.
 *
 * La taille totale d'une trame est donc la somme de ces éléments : taille du header (5 octets) + longueur des données (variable) + checksum (1 octet).
 * Pour calculer la taille totale, on peut utiliser la formule :
 * Taille totale = 5 (header) + Longueur des données + 1 (checksum)
 * La taille totale dépend de la longueur spécifique des données pour chaque trame individuelle.
 */
void CN105Climate::parse(byte inputData) {

    ESP_LOGV("Decoder", "--> %02X [nb: %d]", inputData, this->bytesRead);

    if (!this->foundStart) {                // no packet yet
        if (inputData == HEADER[0]) {
            this->foundStart = true;
            storedInputData[this->bytesRead++] = inputData;
        } else {
            // unknown bytes
        }
    } else {                                // we are getting a packet
        storedInputData[this->bytesRead] = inputData;

        checkHeader(inputData);

        if (this->dataLength != -1) {       // is header complete ?

            if ((this->bytesRead) == this->dataLength + 5) {

                this->processDataPacket();
                this->initBytePointer();
            } else {                                        // packet is still filling
                this->bytesRead++;                          // more data to come
            }
        } else {
            ESP_LOGV("Decoder", "data length toujours pas connu");
            // header is not complete yet
            this->bytesRead++;
        }
    }

}


bool CN105Climate::checkSum() {
    // TODO: use the CN105Climate::checkSum(byte bytes[], int len) function

    uint8_t packetCheckSum = storedInputData[this->bytesRead];
    uint8_t processedCS = 0;

    ESP_LOGV("chkSum", "controling chkSum should be: %02X ", packetCheckSum);

    for (int i = 0;i < this->dataLength + 5;i++) {
        ESP_LOGV("chkSum", "adding %02X to %02X --> ", this->storedInputData[i], processedCS, processedCS + this->storedInputData[i]);
        processedCS += this->storedInputData[i];
    }

    processedCS = (0xfc - processedCS) & 0xff;

    if (packetCheckSum == processedCS) {
        ESP_LOGD("chkSum", "OK-> %02X=%02X ", processedCS, packetCheckSum);
    } else {
        ESP_LOGW("chkSum", "KO-> %02X!=%02X ", processedCS, packetCheckSum);
    }

    return (packetCheckSum == processedCS);
}


void CN105Climate::checkHeader(byte inputData) {
    if (this->bytesRead == 4) {
        if (storedInputData[2] == HEADER[2] && storedInputData[3] == HEADER[3]) {
            ESP_LOGV("Header", "header matches HEADER");
            ESP_LOGV("Header", "[%02X] (%02X) %02X %02X [%02X]<-- header", storedInputData[0], storedInputData[1], storedInputData[2], storedInputData[3], storedInputData[4]);
            ESP_LOGD("Header", "command: (%02X) data length: [%02X]<-- header", storedInputData[1], storedInputData[4]);
            this->command = storedInputData[1];
        }
        this->dataLength = storedInputData[4];
    }
}

bool CN105Climate::processInput(void) {
    bool processed = false;
    while (this->get_hw_serial_()->available()) {
        processed = true;
        int inputData = this->get_hw_serial_()->read();
        parse(inputData);
    }
    return processed;
}

void CN105Climate::processDataPacket() {

    ESP_LOGV(TAG, "processing data packet...");

    this->data = &this->storedInputData[5];

    this->hpPacketDebug(this->storedInputData, this->bytesRead + 1, "READ");

    if (this->checkSum()) {
        // checkPoint of a heatpump response
        this->lastResponseMs = CUSTOM_MILLIS;    //esphome::CUSTOM_MILLIS;        

        // processing the specific command
        processCommand();
    }
}
void CN105Climate::getDataFromResponsePacket() {

    heatpumpStatus receivedStatus{};
    heatpumpSettings receivedSettings{};



    bool statusDidChange = false;

    switch (this->data[0]) {
    case 0x02: {            /* setting information */
        ESP_LOGD("Decoder", "[0x02 is settings]");
        //this->last_received_packet_sensor->publish_state("0x62-> 0x02: Data -> Settings");        
        receivedSettings.connected = true;      // we're here so we're connected (actually not used property)
        receivedSettings.power = lookupByteMapValue(POWER_MAP, POWER, 2, data[3]);
        receivedSettings.iSee = data[4] > 0x08 ? true : false;
        receivedSettings.mode = lookupByteMapValue(MODE_MAP, MODE, 5, receivedSettings.iSee ? (data[4] - 0x08) : data[4]);

        ESP_LOGD("Decoder", "[Power : %s]", receivedSettings.power);
        ESP_LOGD("Decoder", "[iSee  : %d]", receivedSettings.iSee);
        ESP_LOGD("Decoder", "[Mode  : %s]", receivedSettings.mode);

        if (data[11] != 0x00) {
            int temp = data[11];
            temp -= 128;
            receivedSettings.temperature = (float)temp / 2;
            this->tempMode = true;
            ESP_LOGD("Decoder", "tempMode is true");
        } else {
            receivedSettings.temperature = lookupByteMapValue(TEMP_MAP, TEMP, 16, data[5]);
        }

        ESP_LOGD("Decoder", "[Consigne °C: %f]", receivedSettings.temperature);

        receivedSettings.fan = lookupByteMapValue(FAN_MAP, FAN, 6, data[6]);
        ESP_LOGD("Decoder", "[Fan: %s]", receivedSettings.fan);

        receivedSettings.vane = lookupByteMapValue(VANE_MAP, VANE, 7, data[7]);
        ESP_LOGD("Decoder", "[Vane: %s]", receivedSettings.vane);


        receivedSettings.wideVane = lookupByteMapValue(WIDEVANE_MAP, WIDEVANE, 7, data[10] & 0x0F);



        wideVaneAdj = (data[10] & 0xF0) == 0x80 ? true : false;

        ESP_LOGD("Decoder", "[wideVane: %s (adj:%d)]", receivedSettings.wideVane, wideVaneAdj);

        // moved to settingsChanged()
        //currentSettings = receivedSettings;

        if (this->firstRun) {
            this->wantedSettings = receivedSettings;
            this->wantedSettings.hasChanged = false;
            this->wantedSettings.hasBeenSent = false;
            this->wantedSettings.nb_deffered_requests = 0;       // reset the counter which is tested each update_request_interval in buildAndSendRequestsInfoPackets()

            firstRun = false;
        }
        this->iSee_sensor->publish_state(receivedSettings.iSee);

        //this->settingsChanged(receivedSettings, "heatpumpUpdate");
        this->heatpumpUpdate(receivedSettings);

    }


             break;
    case 0x03: {
        /* room temperature reading */
        ESP_LOGD("Decoder", "[0x03 room temperature]");
        //this->last_received_packet_sensor->publish_state("0x62-> 0x03: Data -> Room temperature");        

        if (data[6] != 0x00) {
            int temp = data[6];
            temp -= 128;
            receivedStatus.roomTemperature = (float)temp / 2;
        } else {
            receivedStatus.roomTemperature = lookupByteMapValue(ROOM_TEMP_MAP, ROOM_TEMP, 32, data[3]);
        }
        ESP_LOGD("Decoder", "[Room °C: %f]", receivedStatus.roomTemperature);

        // no change with this packet to currentStatus for operating and compressorFrequency
        receivedStatus.operating = currentStatus.operating;
        receivedStatus.compressorFrequency = currentStatus.compressorFrequency;

        statusDidChange = true;

    }
             break;

    case 0x04:
        /* unknown */
        ESP_LOGI("Decoder", "[0x04 is unknown]");
        //this->last_received_packet_sensor->publish_state("0x62-> 0x04: Data -> Unknown");
        break;

    case 0x05:
        /* timer packet */
        ESP_LOGW("Decoder", "[0x05 is timer packet: not implemented]");
        //this->last_received_packet_sensor->publish_state("0x62-> 0x05: Data -> Timer Packet");
        break;

    case 0x06: {
        /* status */
        ESP_LOGD("Decoder", "[0x06 is status]");
        //this->last_received_packet_sensor->publish_state("0x62-> 0x06: Data -> Heatpump Status");

        // reset counter (because a reply indicates it is connected)
        this->nonResponseCounter = 0;
        receivedStatus.operating = data[4];
        receivedStatus.compressorFrequency = data[3];

        // no change with this packet to roomTemperature
        receivedStatus.roomTemperature = currentStatus.roomTemperature;


        statusDidChange = true;
        // RCVD_PKT_STATUS;
    }
             break;

    case 0x09:
        /* unknown */
        ESP_LOGD("Decoder", "[0x09 is unknown]");
        //this->last_received_packet_sensor->publish_state("0x62-> 0x09: Data -> Unknown");
        break;
    case 0x20:
    case 0x22: {
        ESP_LOGD("Decoder", "[Packet Functions 0x20 et 0x22]");
        //this->last_received_packet_sensor->publish_state("0x62-> 0x20/0x22: Data -> Packet functions");
        if (dataLength == 0x10) {
            if (data[0] == 0x20) {
                functions.setData1(&data[1]);
            } else {
                functions.setData2(&data[1]);
            }

            // RCVD_PKT_FUNCTIONS;
        }

    }
             break;

    default:
        ESP_LOGW("Decoder", "type de packet [%02X] <-- inconnu et inattendu", data[0]);
        //this->last_received_packet_sensor->publish_state("0x62-> ?? : Data -> Unknown");
        break;
    }

    if (statusDidChange) {
        this->statusChanged(receivedStatus);
    }
}

void CN105Climate::updateSuccess() {
    ESP_LOGI(TAG, "Last heatpump data update successful!");
    //this->last_received_packet_sensor->publish_state("0x61: update success");
    // as the update was successful, we can set currentSettings to wantedSettings        
    // even if the next settings request will do the same.
    if (this->wantedSettings.hasChanged) {
        ESP_LOGI(TAG, "And it was a wantedSetting ACK!");
        this->wantedSettings.hasChanged = false;
        this->wantedSettings.hasBeenSent = false;
        this->wantedSettings.nb_deffered_requests = 0;       // reset the counter which is tested each update_request_interval in buildAndSendRequestsInfoPackets()
        //this->settingsChanged(this->wantedSettings, "WantedSettingsUpdateSuccess");
        this->wantedSettingsUpdateSuccess(this->wantedSettings);
    } else {
        ESP_LOGI(TAG, "And it was a setExternalTemperature() ACK!");
        // sendind the remoteTemperature would have more sense but we don't know it
        // the hp will sent if later
        //this->settingsChanged(this->currentSettings, "ExtTempUpdateSuccess");
        this->extTempUpdateSuccess();

    }
    /*this->currentSettings.power = this->wantedSettings.power;
    this->currentSettings.mode = this->wantedSettings.mode;
    this->currentSettings.fan = this->wantedSettings.fan;
    this->currentSettings.vane = this->wantedSettings.vane;
    //this->currentSettings.wideVane = this->wantedSettings.wideVane;
    this->currentSettings.temperature = this->wantedSettings.temperature;*/

    if (!this->autoUpdate) {
        this->buildAndSendRequestsInfoPackets();
    }
}

void CN105Climate::processCommand() {
    switch (this->command) {
    case 0x61:  /* last update was successful */
        this->updateSuccess();
        break;

    case 0x62:  /* packet contains data (room °C, settings, timer, status, or functions...)*/
        this->getDataFromResponsePacket();
        break;
    case 0x7a:
        ESP_LOGI(TAG, "--> Heatpump did reply: connection success! <--");
        this->isHeatpumpConnected_ = true;
        //this->last_received_packet_sensor->publish_state("0x7A: Connection success");
        programUpdateInterval();        // we know a check in this method is done on autoupdate value        
        break;
    default:
        break;
    }
}


void CN105Climate::statusChanged(heatpumpStatus status) {

    this->debugStatus("received", status);

    if (status != currentStatus) {
        this->debugStatus("current", currentStatus);
    }

    currentStatus.operating = status.operating;
    currentStatus.compressorFrequency = status.compressorFrequency;
    currentStatus.roomTemperature = status.roomTemperature;
    this->current_temperature = currentStatus.roomTemperature;

    this->updateAction();       // update action info on HA climate component

    this->publish_state();
    this->compressor_frequency_sensor->publish_state(currentStatus.compressorFrequency);
}


void CN105Climate::publishStateToHA(heatpumpSettings settings) {
    checkPowerAndModeSettings(settings);
    this->updateAction();       // update action info on HA climate component
    checkFanSettings(settings);
    checkVaneSettings(settings);
    // HA Temp
    this->target_temperature = settings.temperature;

    // CurrentSettings update
    this->currentSettings.temperature = settings.temperature;
    this->currentSettings.iSee = settings.iSee;
    this->currentSettings.connected = true;

    // publish to HA
    this->publish_state();

}


void CN105Climate::wantedSettingsUpdateSuccess(heatpumpSettings settings) {
    // settings correponds to fresh wanted settings
    ESP_LOGD(LOG_ACTION_EVT_TAG, "WantedSettings update success");
    // update HA states thanks to wantedSettings
    this->publishStateToHA(settings);

    // as wantedSettings has been received with ACK by the heatpump
    // we can update the surrentSettings
    this->currentSettings = this->wantedSettings;
    this->debugSettings("current", currentSettings);
}

void CN105Climate::extTempUpdateSuccess() {
    ESP_LOGD(LOG_ACTION_EVT_TAG, "External C° update success");
    // can retreive room °C from currentStatus.roomTemperature because 
    // set_remote_temperature() is optimistic and has recorded it 
    this->current_temperature = currentStatus.roomTemperature;
    this->publish_state();
}

void CN105Climate::heatpumpUpdate(heatpumpSettings settings) {
    // settings correponds to current settings 
    ESP_LOGD(LOG_ACTION_EVT_TAG, "Settings received");

    heatpumpSettings& wanted = wantedSettings;  // for casting purpose
    if (settings == wanted) {
        // settings correponds to fresh received settings
        if (wantedSettings.hasChanged) {
            ESP_LOGW(LOG_SETTINGS_TAG, "receivedSettings match wanted ones, but wantedSettings.hasChanged is true, setting it to false in settingsChanged method");
        }

        // no difference wt wantedSettings and received ones
        // by security tag wantedSettings hasChanged to false
        wantedSettings.hasChanged = false;
        this->wantedSettings.hasBeenSent = false;
        this->wantedSettings.nb_deffered_requests = 0;
    } else {
        this->debugSettings("current", this->currentSettings);
        this->debugSettings("wanted", this->wantedSettings);

        // here wantedSettings and currentSettings are different
        // we want to know why
        if (wantedSettings.hasChanged) {
            this->debugSettings("received", settings);
            // it's because user did ask a change throuth HA
            // we have nothing to do, because this change will trigger a packet send from 
            // the loop() method
            ESP_LOGW(LOG_ACTION_EVT_TAG, "wantedSettings is true, and we received an info packet");
        } else {

            // it's because of an IR remote control update
            this->publishStateToHA(settings);
            this->debugSettings("receivedIR", settings);
        }
    }
}

/*
void CN105Climate::settingsChanged(heatpumpSettings settings, const char* source) {


    if (strcmp(source, "WantedSettingsUpdateSuccess") == 0) {
        // settings correponds to fresh wanted settings
        ESP_LOGD(LOG_ACTION_EVT_TAG, "WantedSettings update success");
        // update HA states thanks to wantedSettings
        this->publishStateToHA(settings);

        // as wantedSettings has been received with ACK by the heatpump
        // we can update the surrentSettings
        this->currentSettings = this->wantedSettings;
        this->debugSettings("current", currentSettings);
    }

    if (strcmp(source, "ExtTempUpdateSuccess")) {
        // settings correponds to current settings but that's not important
        ESP_LOGD(LOG_ACTION_EVT_TAG, "External C° update success");
        // can retreive room °C from currentStatus.roomTemperature because
        // set_remote_temperature() is optimistic and has recorded it
        this->current_temperature = currentStatus.roomTemperature;
    }

    if (strcmp(source, "heatpumpUpdate")) {
        // settings correponds to current settings
        ESP_LOGD(LOG_ACTION_EVT_TAG, "Settings received");

        heatpumpSettings& wanted = wantedSettings;  // for casting purpose
        if (wanted == settings) {
            // settings correponds to fresh received settings
            if (wantedSettings.hasChanged) {
                ESP_LOGW(LOG_SETTINGS_TAG, "receivedSettings match wanted ones, but wantedSettings.hasChanged is true, setting it to false in settingsChanged method");
            }

            // no difference wt wantedSettings and received ones
            // by security tag wantedSettings hasChanged to false
            wantedSettings.hasChanged = false;
            this->wantedSettings.nb_deffered_requests = 0;
        } else {
            // here wantedSettings and currentSettings are different
            // we want to know why
            if (wantedSettings.hasChanged) {
                // it's because user did ask a change throuth HA
                // we have nothing to do, because this change will trigger a packet send from
                // the loop() method
                ESP_LOGW(LOG_ACTION_EVT_TAG, "wantedSettings is true, and we received an info packet");
            } else {
                // it's because of an IR remote control update
                this->publishStateToHA(settings);
                this->debugSettings("receivedIR", settings);
            }
        }
    }
}*/

void CN105Climate::checkVaneSettings(heatpumpSettings& settings) {
    /* ******** HANDLE MITSUBISHI VANE CHANGES ********
         * const char* VANE_MAP[7]        = {"AUTO", "1", "2", "3", "4", "5", "SWING"};
         */
    if (this->hasChanged(currentSettings.vane, settings.vane, "vane")) { // vane setting change ?
        ESP_LOGI(TAG, "vane setting changed");
        currentSettings.vane = settings.vane;

        if (strcmp(currentSettings.vane, "SWING") == 0) {
            this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
        } else {
            this->swing_mode = climate::CLIMATE_SWING_OFF;
        }
        ESP_LOGD(TAG, "Swing mode is: %i", this->swing_mode);
    }

    if (this->hasChanged(this->vane->state.c_str(), settings.vane, "select vane")) {
        ESP_LOGI(TAG, "vane setting (extra select component) changed");
        this->vane->publish_state(currentSettings.vane);
    }
}
void CN105Climate::checkFanSettings(heatpumpSettings& settings) {
    /*
         * ******* HANDLE FAN CHANGES ********
         *
         * const char* FAN_MAP[6]         = {"AUTO", "QUIET", "1", "2", "3", "4"};
         */
         // currentSettings.fan== NULL is true when it is the first time we get en answer from hp

    if (this->hasChanged(currentSettings.fan, settings.fan, "fan")) { // fan setting change ?
        ESP_LOGI(TAG, "fan setting changed");
        currentSettings.fan = settings.fan;
        if (strcmp(currentSettings.fan, "QUIET") == 0) {
            this->fan_mode = climate::CLIMATE_FAN_QUIET;
        } else if (strcmp(currentSettings.fan, "1") == 0) {
            this->fan_mode = climate::CLIMATE_FAN_LOW;
        } else if (strcmp(currentSettings.fan, "2") == 0) {
            this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
        } else if (strcmp(currentSettings.fan, "3") == 0) {
            this->fan_mode = climate::CLIMATE_FAN_MIDDLE;
        } else if (strcmp(currentSettings.fan, "4") == 0) {
            this->fan_mode = climate::CLIMATE_FAN_HIGH;
        } else { //case "AUTO" or default:
            this->fan_mode = climate::CLIMATE_FAN_AUTO;
        }
        ESP_LOGD(TAG, "Fan mode is: %i", this->fan_mode);
    }
}
void CN105Climate::checkPowerAndModeSettings(heatpumpSettings& settings) {
    // currentSettings.power== NULL is true when it is the first time we get en answer from hp
    if (this->hasChanged(currentSettings.power, settings.power, "power") ||
        this->hasChanged(currentSettings.mode, settings.mode, "mode")) {           // mode or power change ?

        ESP_LOGI(TAG, "power or mode changed");
        currentSettings.power = settings.power;
        currentSettings.mode = settings.mode;

        if (strcmp(currentSettings.power, "ON") == 0) {
            if (strcmp(currentSettings.mode, "HEAT") == 0) {
                this->mode = climate::CLIMATE_MODE_HEAT;
            } else if (strcmp(currentSettings.mode, "DRY") == 0) {
                this->mode = climate::CLIMATE_MODE_DRY;
            } else if (strcmp(currentSettings.mode, "COOL") == 0) {
                this->mode = climate::CLIMATE_MODE_COOL;
                /*if (cool_setpoint != currentSettings.temperature) {
                    cool_setpoint = currentSettings.temperature;
                    save(currentSettings.temperature, cool_storage);
                }*/
            } else if (strcmp(currentSettings.mode, "FAN") == 0) {
                this->mode = climate::CLIMATE_MODE_FAN_ONLY;
            } else if (strcmp(currentSettings.mode, "AUTO") == 0) {
                this->mode = climate::CLIMATE_MODE_HEAT_COOL;
            } else {
                ESP_LOGW(
                    TAG,
                    "Unknown climate mode value %s received from HeatPump",
                    currentSettings.mode
                );
            }
        } else {
            this->mode = climate::CLIMATE_MODE_OFF;
        }
    }
}

