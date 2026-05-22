#ifndef ESPNOW_MANAGER_H
#define ESPNOW_MANAGER_H

#include <Arduino.h>
#include <esp_err.h>
#include <esp_now.h>
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
    static void resetErrorLogAckState();
    static bool waitForErrorLogAck(uint16_t expectedLines, uint32_t timeoutMs, uint16_t &ackLinesOut);
#endif

    static bool begin();

#ifdef IS_LOGGER
    static esp_err_t sendTelemetry(const TelemetryMsg &msg);
#endif

#ifdef IS_DISPLAY
    static esp_err_t sendImuFeedback(const ImuFeedbackMsg &msg);
    static esp_err_t sendErrorLogAck(uint16_t linesWritten);
    static esp_err_t sendTrackConfig(const TrackConfigMsg &msg);
    static bool consumeErrorLogStart(uint16_t &totalLines);
    static bool consumeErrorLogEnd(uint16_t &totalLines);
    static bool popErrorLogLine(ErrorLogLineMsg &msg);
    static uint16_t getErrorLogDroppedLines();
#endif

#ifdef IS_LOGGER
    static bool consumeTrackConfig(TrackConfigMsg &msg);
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