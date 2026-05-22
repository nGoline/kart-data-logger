#include "EspNowManager.h"

#include <WiFi.h>
#include <esp_wifi.h>

#ifdef IS_DISPLAY
#include <freertos/queue.h>
#endif

bool EspNowManager::newDataAvailable = false;
TelemetryMsg EspNowManager::lastTelemetry = {};
#ifdef IS_LOGGER
ImuFeedbackMsg EspNowManager::lastImuFeedback = {};
uint32_t EspNowManager::imuFeedbackCounter = 0;
static volatile bool s_errorLogAckReceived = false;
static volatile uint16_t s_errorLogAckLines = 0;
static TrackConfigMsg s_lastTrackConfig = {};
static volatile bool s_trackConfigReceived = false;
#endif

#ifdef IS_DISPLAY
static QueueHandle_t s_errorLogLineQueue = nullptr;
static volatile bool s_errorLogStartEvent = false;
static volatile bool s_errorLogEndEvent = false;
static volatile uint16_t s_errorLogStartLines = 0;
static volatile uint16_t s_errorLogEndLines = 0;
static volatile uint16_t s_errorLogDroppedLines = 0;
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

#ifdef IS_DISPLAY
    if (s_errorLogLineQueue == nullptr) {
        s_errorLogLineQueue = xQueueCreate(24, sizeof(ErrorLogLineMsg));
        if (s_errorLogLineQueue == nullptr) {
            log_e("Failed to create error log line queue.");
            return false;
        }
    }
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
esp_err_t EspNowManager::sendImuFeedback(const ImuFeedbackMsg &msg) {
    uint8_t bcastAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_err_t res = esp_now_send(bcastAddr, (uint8_t*)&msg, sizeof(msg));
    if (res != ESP_OK) {
        log_w("IMU feedback send failed! Error code: %d", res);
    }
    return res;
}

esp_err_t EspNowManager::sendTrackConfig(const TrackConfigMsg &msg) {
    uint8_t bcastAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_err_t res = esp_now_send(bcastAddr, (uint8_t*)&msg, sizeof(msg));
    if (res != ESP_OK) {
        log_w("Track config send failed! Error code: %d", res);
    }
    return res;
}

esp_err_t EspNowManager::sendErrorLogAck(uint16_t linesWritten) {
    ErrorLogControlMsg msg = {};
    msg.type = MSG_ERROR_LOG_ACK;
    msg.totalLines = linesWritten;

    uint8_t bcastAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_err_t res = esp_now_send(bcastAddr, (uint8_t *)&msg, sizeof(msg));
    if (res != ESP_OK) {
        log_w("Error log ACK send failed! Error code: %d", res);
    }
    return res;
}

bool EspNowManager::consumeErrorLogStart(uint16_t &totalLines) {
    if (!s_errorLogStartEvent) {
        return false;
    }

    totalLines = s_errorLogStartLines;
    s_errorLogStartEvent = false;
    return true;
}

bool EspNowManager::consumeErrorLogEnd(uint16_t &totalLines) {
    if (!s_errorLogEndEvent) {
        return false;
    }

    totalLines = s_errorLogEndLines;
    s_errorLogEndEvent = false;
    return true;
}

bool EspNowManager::popErrorLogLine(ErrorLogLineMsg &msg) {
    if (s_errorLogLineQueue == nullptr) {
        return false;
    }

    return xQueueReceive(s_errorLogLineQueue, &msg, 0) == pdTRUE;
}

uint16_t EspNowManager::getErrorLogDroppedLines() {
    return s_errorLogDroppedLines;
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

void EspNowManager::resetErrorLogAckState() {
    s_errorLogAckReceived = false;
    s_errorLogAckLines = 0;
}

bool EspNowManager::consumeTrackConfig(TrackConfigMsg &msg) {
    if (!s_trackConfigReceived) {
        return false;
    }
    msg = s_lastTrackConfig;
    s_trackConfigReceived = false;
    return true;
}

bool EspNowManager::waitForErrorLogAck(uint16_t expectedLines, uint32_t timeoutMs, uint16_t &ackLinesOut) {
    uint32_t start = millis();
    while ((millis() - start) < timeoutMs) {
        if (s_errorLogAckReceived) {
            ackLinesOut = s_errorLogAckLines;
            return s_errorLogAckLines == expectedLines;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    ackLinesOut = 0;
    return false;
}
#endif

// Shared Receiver Logic
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void EspNowManager::onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
#else
void EspNowManager::onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
#endif
    if (len <= 0) {
        return;
    }

    if (data[0] == MSG_TELEMETRY) {
        static bool warnedTelemetrySizeMismatch = false;
        if (!warnedTelemetrySizeMismatch && len != (int)sizeof(TelemetryMsg)) {
            log_w("Telemetry size mismatch: rx=%d local=%u", len, (unsigned)sizeof(TelemetryMsg));
            warnedTelemetrySizeMismatch = true;
        }

        TelemetryMsg tmp = {};
        size_t copyLen = (len < (int)sizeof(TelemetryMsg)) ? (size_t)len : sizeof(TelemetryMsg);
        memcpy(&tmp, data, copyLen);
        lastTelemetry = tmp;
        newDataAvailable = true;
    } else if (len >= (int)sizeof(ImuFeedbackMsg) && data[0] == MSG_IMU_FEEDBACK) {
#ifdef IS_LOGGER
        memcpy(&lastImuFeedback, data, sizeof(ImuFeedbackMsg));
        imuFeedbackCounter++;
#endif
    } else if (len >= (int)sizeof(ErrorLogControlMsg) && data[0] == MSG_ERROR_LOG_ACK) {
#ifdef IS_LOGGER
        ErrorLogControlMsg ack = {};
        memcpy(&ack, data, sizeof(ErrorLogControlMsg));
        s_errorLogAckLines = ack.totalLines;
        s_errorLogAckReceived = true;
        log_i("Received error log ACK for %u lines.", ack.totalLines);
#endif
    } else if (len >= (int)sizeof(ErrorLogControlMsg) && data[0] == MSG_ERROR_LOG_START) {
#ifdef IS_DISPLAY
        ErrorLogControlMsg start = {};
        memcpy(&start, data, sizeof(ErrorLogControlMsg));
        if (s_errorLogLineQueue != nullptr) {
            xQueueReset(s_errorLogLineQueue);
        }
        s_errorLogDroppedLines = 0;
        s_errorLogStartLines = start.totalLines;
        s_errorLogStartEvent = true;
        s_errorLogEndEvent = false;
        log_i("Error log transfer started (%u lines).", start.totalLines);
#endif
    } else if (len >= (int)sizeof(ErrorLogLineMsg) && data[0] == MSG_ERROR_LOG_LINE) {
#ifdef IS_DISPLAY
        ErrorLogLineMsg lineMsg = {};
        memcpy(&lineMsg, data, sizeof(ErrorLogLineMsg));
        if (s_errorLogLineQueue == nullptr || xQueueSend(s_errorLogLineQueue, &lineMsg, 0) != pdTRUE) {
            s_errorLogDroppedLines++;
        }
#endif
    } else if (len >= (int)sizeof(ErrorLogControlMsg) && data[0] == MSG_ERROR_LOG_END) {
#ifdef IS_DISPLAY
        ErrorLogControlMsg end = {};
        memcpy(&end, data, sizeof(ErrorLogControlMsg));
        s_errorLogEndLines = end.totalLines;
        s_errorLogEndEvent = true;
        log_i("Error log transfer ended (%u lines expected).", end.totalLines);
#endif
    } else if (len >= (int)sizeof(TrackConfigMsg) && data[0] == MSG_TRACK_CONFIG) {
#ifdef IS_LOGGER
        memcpy(&s_lastTrackConfig, data, sizeof(TrackConfigMsg));
        s_trackConfigReceived = true;
        log_i("Track config received: left=(%.6f,%.6f) right=(%.6f,%.6f) valid=%d",
              s_lastTrackConfig.leftLat, s_lastTrackConfig.leftLng,
              s_lastTrackConfig.rightLat, s_lastTrackConfig.rightLng,
              (int)s_lastTrackConfig.valid);
#endif
    } else {
        log_w("Received non-telemetry packet. Type: %d, Len: %d", data[0], len);
    }
}