#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "EspNowProtocol.h"

class LogManager {
public:
    LogManager();
    
    // Pass the shared SPI Mutex here
    bool begin(SemaphoreHandle_t spiMutex);
    
    // The background task that writes to SD
    static void task(void* param);

    // SD lifecycle helpers for logger->display error log transfer.
    bool beginHelmetErrorLog();
    bool appendHelmetErrorLine(const char* line);
    bool finalizeHelmetErrorLog(uint16_t expectedLines, uint16_t writtenLines, uint16_t droppedLines, bool writeFailed);
    bool finalizeHelmetErrorLogAndAck(uint16_t expectedLines, uint16_t writtenLines, uint16_t droppedLines, bool writeFailed);

    bool isSdAvailable() const;

    // Queue to receive data from the EspNowManager
    static QueueHandle_t logQueue;

private:
    SemaphoreHandle_t _spiMutex;
    bool _sdAvailable = false;
    bool _clockSynced = false;
    String _currentFileName;
    
    void createNewFile();
    bool hasValidGpsTime(const TelemetryMsg &msg) const;
    bool syncClockFromTelemetry(const TelemetryMsg &msg);
    bool ensureCurrentLogFile(const TelemetryMsg &msg);
};

#endif