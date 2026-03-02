#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include "TelemetryData.h"
#include "StorageManager.h"

class LogManager {
public:
    LogManager();
    bool begin(QueueHandle_t telemetryQueue);
    static void task(void* param);

private:
    QueueHandle_t _queue = NULL;
    StorageManager _storage;
};

#endif
