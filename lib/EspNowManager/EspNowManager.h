#ifndef ESPNOW_MANAGER_H
#define ESPNOW_MANAGER_H

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "EspNowProtocol.h"

// Define our dedicated racing channel
#define ESPNOW_CHANNEL 1

class EspNowManager {
public:
    static bool newDataAvailable;
    static TelemetryMsg lastTelemetry;

#ifdef IS_LOGGER
    static ImuFeedbackMsg lastImuFeedback;
    static uint32_t imuFeedbackCounter;
    static bool getLatestImuFeedback(ImuFeedbackMsg &msg, uint32_t &counter);
#endif

    static bool begin();

#ifdef IS_LOGGER
    static esp_err_t sendTelemetry(const TelemetryMsg &msg);
#endif

#ifdef IS_DISPLAY
    static esp_err_t sendImuFeedback(const ImuFeedbackMsg &msg);
#endif

private:
    // Signature compatibility for different ESP32 Core versions
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    static void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len);
#else
    static void onDataRecv(const uint8_t *mac, const uint8_t *data, int len);
#endif
};

#endif