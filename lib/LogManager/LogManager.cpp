#include "LogManager.h"
#include "EspNowManager.h"

#include <sys/time.h>

namespace {
constexpr uint64_t kMinValidGpsEpochMs = 1609459200000ULL; // 2021-01-01 UTC
}

QueueHandle_t LogManager::logQueue = NULL;

LogManager::LogManager() {}

bool LogManager::isSdAvailable() const {
    return _sdAvailable;
}

#if !defined(JC3248W535)
bool LogManager::begin(SemaphoreHandle_t spiMutex) {
    _spiMutex = spiMutex;
#else
bool LogManager::begin() {
#endif
    // Ensure SD CS is High (Disabled) before we even start
    #if !defined(JC3248W535)
    pinMode(TF_CS, OUTPUT); 
    digitalWrite(TF_CS, HIGH);
    #endif

    logQueue = xQueueCreate(20, sizeof(TelemetryMsg));

    // Try to initialize SD card
    #if defined(JC3248W535)
    SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
    if (!SD_MMC.begin("/sdmmc", true, false, 20000)) {
        log_e("SD Card Mount Failed");
        _sdAvailable = false;
    } else {
        log_i("LogManager: SD Card Initialized.");
        _sdAvailable = true;
    }
    #else
    if (xSemaphoreTake(_spiMutex, pdMS_TO_TICKS(1000))) {
        // CYD SD is usually on VSPI (Pins 5, 18, 19, 23)
        if (!SD.begin(TF_CS)) {
            log_e("SD Card Mount Failed on Pin %d", TF_CS);
            _sdAvailable = false;
        } else {
            log_i("LogManager: SD Card Initialized.");
            _sdAvailable = true;
        }
        xSemaphoreGive(_spiMutex);
    }
    #endif

    xTaskCreatePinnedToCore(LogManager::task, "LogTask", 4096, this, 1, NULL, 0);
    return _sdAvailable;
}

bool LogManager::beginHelmetErrorLog() {
    if (!_sdAvailable) {
        log_w("LogManager: SD unavailable for helmet_error.log start.");
        return false;
    }

    #if !defined(JC3248W535)
    if (!xSemaphoreTake(_spiMutex, pdMS_TO_TICKS(500))) {
        log_w("LogManager: SPI timeout while starting helmet_error.log.");
        return false;
    }
    #endif

    #if defined(JC3248W535)
    if (SD_MMC.exists("/helmet_error.log")) {
        SD_MMC.remove("/helmet_error.log");
    }

    File f = SD_MMC.open("/helmet_error.log", FILE_WRITE);
    if (!f) {
        log_w("LogManager: Failed to create /helmet_error.log.");
        return false;
    }
    #else
    if (SD.exists("/helmet_error.log")) {
        SD.remove("/helmet_error.log");
    }

    File f = SD.open("/helmet_error.log", FILE_WRITE);
    if (!f) {
        xSemaphoreGive(_spiMutex);
        log_w("LogManager: Failed to create /helmet_error.log.");
        return false;
    }
    #endif

    f.close();
    #if !defined(JC3248W535)
    xSemaphoreGive(_spiMutex);
    #endif
    return true;
}

bool LogManager::appendHelmetErrorLine(const char* line) {
    if (!_sdAvailable) {
        return false;
    }

    if (line == nullptr) {
        return false;
    }

    #if defined(JC3248W535)
    File f = SD_MMC.open("/helmet_error.log", FILE_APPEND);
    if (!f) {
        log_w("LogManager: Failed to open /helmet_error.log for append.");
        return false;
    }
    #else
    if (!xSemaphoreTake(_spiMutex, pdMS_TO_TICKS(500))) {
        log_w("LogManager: SPI timeout while appending helmet_error.log.");
        return false;
    }

    File f = SD.open("/helmet_error.log", FILE_APPEND);
    if (!f) {
        xSemaphoreGive(_spiMutex);
        log_w("LogManager: Failed to open /helmet_error.log for append.");
        return false;
    }
    #endif

    f.println(line);
    f.close();
    #if !defined(JC3248W535)
    xSemaphoreGive(_spiMutex);
    #endif
    return true;
}

bool LogManager::finalizeHelmetErrorLog(uint16_t expectedLines, uint16_t writtenLines, uint16_t droppedLines, bool writeFailed) {
    if (!_sdAvailable) {
        return false;
    }

    const bool complete = (!writeFailed && droppedLines == 0 && expectedLines == writtenLines);
    if (!complete) {
        log_w("LogManager: helmet_error.log incomplete. expected=%u written=%u dropped=%u writeFailed=%d",
              expectedLines, writtenLines, droppedLines, writeFailed ? 1 : 0);
    }

    return complete;
}

bool LogManager::finalizeHelmetErrorLogAndAck(uint16_t expectedLines, uint16_t writtenLines, uint16_t droppedLines, bool writeFailed) {
    const bool complete = finalizeHelmetErrorLog(expectedLines, writtenLines, droppedLines, writeFailed);
    if (!complete) {
        return false;
    }

    if (EspNowManager::sendErrorLogAck(writtenLines) != ESP_OK) {
        log_w("LogManager: failed to send ACK for helmet_error.log.");
        return false;
    }

    log_i("LogManager: ACK sent for helmet_error.log (%u lines).", writtenLines);
    return true;
}

void LogManager::createNewFile() {
    // Basic incrementing file name: log_0.csv, log_1.csv...
    int i = 0;
    #if defined(JC3248W535)
    while (SD_MMC.exists("/log_" + String(i) + ".csv")) i++;
    #else
    while (SD.exists("/log_" + String(i) + ".csv")) i++;
    #endif
    _currentFileName = "/log_" + String(i) + ".csv";
    
    #if defined(JC3248W535)
    File f = SD_MMC.open(_currentFileName, FILE_WRITE);
    #else
    File f = SD.open(_currentFileName, FILE_WRITE);
    #endif
    if (f) {
        f.println("epoch,speed,totalGForce,gForceX,gForceY,steering_angle,sats,lat,lng"); //CSV header
        f.close();
        log_i("LogManager: Started new log: %s", _currentFileName.c_str());
    }
}

void LogManager::startSession() {
    if (!_sdAvailable || _sessionActive || _pendingStart) return;
    _pendingStart = true;
    log_i("LogManager: session start requested.");
}

void LogManager::stopSession() {
    if (!_sessionActive && !_pendingStart) return;
    _pendingStart = false;
    _pendingStop = true;
    log_i("LogManager: session stop requested.");
}

bool LogManager::hasValidGpsTime(const TelemetryMsg &msg) const {
    return msg.sats > 0 && msg.timestamp >= kMinValidGpsEpochMs;
}

bool LogManager::syncClockFromTelemetry(const TelemetryMsg &msg) {
    if (!hasValidGpsTime(msg)) return false;

    struct timeval tv = {};
    tv.tv_sec = (time_t)(msg.timestamp / 1000ULL);
    tv.tv_usec = (suseconds_t)((msg.timestamp % 1000ULL) * 1000ULL);

    if (settimeofday(&tv, nullptr) != 0) {
        log_w("LogManager: failed to sync system time from GPS epoch %llu.", msg.timestamp);
        return false;
    }

    if (!_clockSynced) log_i("LogManager: system time synced from GPS.");
    _clockSynced = true;
    return true;
}

void LogManager::task(void* param) {
    LogManager* self = (LogManager*)param;
    TelemetryMsg msg;

    for (;;) {
        // Process pending start/stop requests (flags set by any task).
        if (self->_pendingStop) {
            self->_pendingStop   = false;
            self->_sessionActive = false;
            self->_currentFileName = "";
            log_i("LogManager: session stopped.");
        }
        if (self->_pendingStart && !self->_sessionActive) {
            self->_pendingStart = false;
            self->createNewFile();
            self->_sessionActive = true;
            log_i("LogManager: session active.");
        }

        // Use a timeout so we can re-check flags even when the queue is empty.
        if (!xQueueReceive(logQueue, &msg, pdMS_TO_TICKS(100))) continue;

        if (!self->_sdAvailable || !self->_sessionActive) continue;
        if (msg.sats == 0) continue;
        if (self->_currentFileName.isEmpty()) continue;

        // Best-effort clock sync from GPS (non-blocking; doesn't gate logging).
        if (!self->_clockSynced) self->syncClockFromTelemetry(msg);

        #if defined(JC3248W535)
        File f = SD_MMC.open(self->_currentFileName, FILE_APPEND);
        #else
        if (!xSemaphoreTake(self->_spiMutex, pdMS_TO_TICKS(500))) {
            log_w("LogManager: SPI Timeout! Missed a log entry.");
            continue;
        }
        File f = SD.open(self->_currentFileName, FILE_APPEND);
        #endif

        if (f) {
            f.printf("%llu,%.1f,%.2f,%.2f,%.2f,%.1f,%d,%.6f,%.6f\n",
                     msg.timestamp, msg.speedKmph, msg.totalGForce,
                     msg.gForceX, msg.gForceY, msg.steeringAngle, msg.sats, msg.lat, msg.lng);
            f.close();
        }

        #if !defined(JC3248W535)
        xSemaphoreGive(self->_spiMutex);
        #endif
    }
}