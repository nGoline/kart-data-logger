#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <Arduino.h>
#include <LittleFS.h>

class StorageManager {
public:
    StorageManager();
    bool begin();
    void logData(uint32_t timestamp, double lat, double lng, double speed, float batVolts);
    void flush();

private:
    File _logFile;
    uint32_t _lastFlushTime;
    const uint32_t FLUSH_INTERVAL_MS = 5000; // Salva fisicamente a cada 5 segundos
};

#endif