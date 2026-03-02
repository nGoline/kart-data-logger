#include "LogManager.h"

LogManager::LogManager() {}

bool LogManager::begin(QueueHandle_t telemetryQueue) {
    _queue = telemetryQueue;

    if (!_storage.begin()) {
        Serial.println("[LogManager] Failed to init storage");
        return false;
    }

    // Start the logging task on Core 1 (non-blocking for Core 0 tasks)
    xTaskCreatePinnedToCore(
        LogManager::task,
        "LogTask",
        8192,
        this,
        1,
        NULL,
        1 // Core 1
    );

    return true;
}

void LogManager::task(void* param) {
    LogManager* self = (LogManager*)param;

    TelemetryData sample;
    const TickType_t qTimeout = pdMS_TO_TICKS(2000); // wait up to 2s for data
    uint32_t lastFlush = millis();

    for (;;) {
        if (self->_queue && xQueueReceive(self->_queue, &sample, qTimeout) == pdTRUE) {
            self->_storage.logTelemetry(sample);
        }

        // Periodic flush to ensure data reaches the flash regularly
        if (millis() - lastFlush > 5000) {
            self->_storage.flush();
            lastFlush = millis();
        }
    }
}
