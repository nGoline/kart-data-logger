#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <Arduino.h>

#if defined(JC3248W535)
#include "SD_MMC.h"
#else
#include <SD.h>
#include <SPI.h>
#endif

#include "EspNowProtocol.h"

class LogManager {
public:
    LogManager();
    
    #if defined(JC3248W535)
    bool begin();
    #else
    // Pass the shared SPI Mutex here
    bool begin(SemaphoreHandle_t spiMutex);
    #endif

    // The background task that writes to SD
    static void task(void* param);

    // SD lifecycle helpers for logger->display error log transfer.
    bool beginHelmetErrorLog();
    bool appendHelmetErrorLine(const char* line);
    bool finalizeHelmetErrorLog(uint16_t expectedLines, uint16_t writtenLines, uint16_t droppedLines, bool writeFailed);
    bool finalizeHelmetErrorLogAndAck(uint16_t expectedLines, uint16_t writtenLines, uint16_t droppedLines, bool writeFailed);

    bool isSdAvailable() const;

    // Session control — call from any task; file I/O happens inside the log task.
    void startSession();
    void stopSession();
    bool isSessionActive() const { return _sessionActive; }

    // Queue to receive data from the EspNowManager
    static QueueHandle_t logQueue;

private:
    #if !defined(JC3248W535)
    SemaphoreHandle_t _spiMutex;
    #endif
    bool _sdAvailable = false;
    bool _clockSynced = false;
    String _currentFileName;

    // Session state — written by log task, flags set by any task (volatile for cross-task visibility)
    volatile bool _sessionActive  = false;
    volatile bool _pendingStart   = false;
    volatile bool _pendingStop    = false;

    void createNewFile();
    bool hasValidGpsTime(const TelemetryMsg &msg) const;
    bool syncClockFromTelemetry(const TelemetryMsg &msg);
};

#endif