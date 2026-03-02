#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include "TelemetryData.h"

class StorageManager {
public:
    StorageManager();
    bool begin();
    void logData(uint32_t timestamp, double lat, double lng, double speed, float batVolts);
    void logTelemetry(const TelemetryData &t);
    void flush();
    // Safely remove a log file. If the file is currently open, it will close it first.
    bool removeLogFile(const char* name);

private:
    File _logFile;
    String _currentLogName;
    uint32_t _currentLogStartMs = 0;
    const size_t MAX_LOG_FILE_SIZE = 1024 * 1024; // 1MiB rotation threshold
    uint32_t _lastFlushTime;
    const uint32_t FLUSH_INTERVAL_MS = 5000; // Salva fisicamente a cada 5 segundos
    bool openNewLogFile();
    void rotateIfNeeded();
};

#endif