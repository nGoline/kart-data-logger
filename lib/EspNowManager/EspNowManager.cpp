#include "EspNowManager.h"

bool EspNowManager::newDataAvailable = false;
TelemetryMsg EspNowManager::lastTelemetry = {};

#ifdef IS_DISPLAY
bool EspNowManager::isLocked = false;
uint8_t EspNowManager::currentChannel = 1;
#endif

bool EspNowManager::begin() {
    WiFi.mode(WIFI_STA);
    
#ifdef IS_DISPLAY
    WiFi.disconnect();
    log_i("Display mode: Radio initialized. Scanning starting...");
#else
    log_i("Logger mode: Radio initialized. Setting up Broadcast...");
#endif

    if (esp_now_init() != ESP_OK) {
        log_e("ESP-NOW initialization failed!");
        return false;
    }

    esp_now_register_recv_cb(onDataRecv);

#ifdef IS_LOGGER
    // Register Broadcast Peer
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
#endif

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
void EspNowManager::updateScanner() {
    if (isLocked) return;
    static uint32_t lastSwitch = 0;
    if (millis() - lastSwitch > 250) {
        lastSwitch = millis();
        currentChannel++;
        if (currentChannel > 13) currentChannel = 1;
        esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
        log_d("Scanner: Checking Channel %d", currentChannel);
    }
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
#ifdef IS_DISPLAY
        if (!isLocked) {
            isLocked = true;
            log_i("Signal Locked on Channel %d!", currentChannel);
        }
#endif
    } else {
        log_v("Received non-telemetry packet. Type: %d, Len: %d", data[0], len);
    }
}