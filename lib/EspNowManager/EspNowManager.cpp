#include "EspNowManager.h"

bool EspNowManager::newDataAvailable = false;
TelemetryMsg EspNowManager::lastTelemetry = {};
#ifdef IS_LOGGER
ImuFeedbackMsg EspNowManager::lastImuFeedback = {};
uint32_t EspNowManager::imuFeedbackCounter = 0;
#endif

bool EspNowManager::begin() {
    // 1. Put Wi-Fi in Station mode and disconnect from any old saved APs
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.setSleep(false);

    // 2. FORCE THE RADIO CHANNEL
    // We briefly enable promiscuous mode to force the channel change at the MAC level
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    // Verify the channel actually applied
    uint8_t primaryChan;
    wifi_second_chan_t secondChan;
    esp_wifi_get_channel(&primaryChan, &secondChan);
    log_i("Radio active. Hardware locked to Channel %d.", primaryChan);
    
#ifdef IS_DISPLAY
    log_i("Display mode: Radio initialized.");
#else
    log_i("Logger mode: Radio initialized. Setting up Broadcast...");
#endif

    if (esp_now_init() != ESP_OK) {
        log_e("ESP-NOW initialization failed!");
        return false;
    }

    esp_now_register_recv_cb(onDataRecv);

    // Register Broadcast Peer for TX (logger telemetry + display IMU feedback)
    esp_now_peer_info_t bcast = {};
    uint8_t bcastAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    memcpy(bcast.peer_addr, bcastAddr, 6);
    bcast.channel = 0;
    bcast.encrypt = false;

    if (esp_now_add_peer(&bcast) != ESP_OK) {
        log_e("Failed to add broadcast peer!");
        return false;
    }
    log_i("Broadcast peer added successfully.");

    return true;
}

#ifdef IS_LOGGER
esp_err_t EspNowManager::sendTelemetry(const TelemetryMsg &msg) {
    uint8_t bcastAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_err_t res = esp_now_send(bcastAddr, (uint8_t*)&msg, sizeof(msg));
    if (res != ESP_OK) {
        log_w("Telemetry send failed! Error code: %d", res);
    }
    return res;
}
#endif

#ifdef IS_DISPLAY
esp_err_t EspNowManager::sendImuFeedback(const ImuFeedbackMsg &msg) {
    uint8_t bcastAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_err_t res = esp_now_send(bcastAddr, (uint8_t*)&msg, sizeof(msg));
    if (res != ESP_OK) {
        log_w("IMU feedback send failed! Error code: %d", res);
    }
    return res;
}
#endif

#ifdef IS_LOGGER
bool EspNowManager::getLatestImuFeedback(ImuFeedbackMsg &msg, uint32_t &counter) {
    if (imuFeedbackCounter == 0) {
        return false;
    }

    msg = lastImuFeedback;
    counter = imuFeedbackCounter;
    return true;
}
#endif

// Shared Receiver Logic
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void EspNowManager::onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
#else
void EspNowManager::onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
#endif
    if (len >= (int)sizeof(TelemetryMsg) && data[0] == MSG_TELEMETRY) {
        memcpy(&lastTelemetry, data, sizeof(TelemetryMsg));
        newDataAvailable = true;
    } else if (len >= (int)sizeof(ImuFeedbackMsg) && data[0] == MSG_IMU_FEEDBACK) {
#ifdef IS_LOGGER
        memcpy(&lastImuFeedback, data, sizeof(ImuFeedbackMsg));
        imuFeedbackCounter++;
#endif
    } else {
        log_w("Received non-telemetry packet. Type: %d, Len: %d", data[0], len);
    }
}