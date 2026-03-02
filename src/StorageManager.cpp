#include "StorageManager.h"
#include <time.h>

StorageManager::StorageManager() : _lastFlushTime(0) {}

bool StorageManager::begin() {
    // Tenta montar o LittleFS. Se falhar (ex: primeiro boot), ele formata a partição (true)
    if (!LittleFS.begin(true)) { 
        return false;
    }
    // Create a new log file named with the current millis() to avoid collisions
    _currentLogStartMs = millis();
    return openNewLogFile();
}

void StorageManager::logData(uint32_t timestamp, double lat, double lng, double speed, float batVolts) {
    if (!_logFile) {
        // attempt to reopen
        if (!openNewLogFile()) return;
    }

    rotateIfNeeded();

    _logFile.printf("%lu,%.6f,%.6f,%.2f,%.2f\n", timestamp, lat, lng, speed, batVolts);

    // Gerencia o flush automaticamente para garantir integridade caso a energia caia
    if (millis() - _lastFlushTime > FLUSH_INTERVAL_MS) {
        flush();
    }
}

void StorageManager::logTelemetry(const TelemetryData &t) {
    if (!_logFile) {
        if (!openNewLogFile()) return;
    }

    rotateIfNeeded();

    // CSV: Timestamp,Latitude,Longitude,Speed_kmh,GForce,Satellites,HasFix
    // Format timestamp as ISO8601 UTC if possible (t.lastUpdate is epoch seconds)
    char timebuf[32] = {0};
    if (t.lastUpdate >= 1000000000UL) {
        time_t tt = (time_t)t.lastUpdate;
        struct tm tm;
        gmtime_r(&tt, &tm);
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    } else {
        // fallback to seconds since boot
        snprintf(timebuf, sizeof(timebuf), "%lu", (unsigned long)t.lastUpdate);
    }

    _logFile.printf("%s,%.6f,%.6f,%.2f,%.3f,%u,%d\n",
                    timebuf,
                    t.lat,
                    t.lng,
                    t.speed,
                    t.gForce,
                    (unsigned int)t.sats,
                    t.hasFix ? 1 : 0);

    if (millis() - _lastFlushTime > FLUSH_INTERVAL_MS) {
        flush();
    }
}

void StorageManager::flush() {
    if (_logFile) {
        _logFile.flush();
        _lastFlushTime = millis();
    }
}

bool StorageManager::openNewLogFile() {
    // Close existing if open
    if (_logFile) {
        _logFile.close();
    }

    _currentLogName = "/telemetry_" + String(_currentLogStartMs) + ".csv";
    _logFile = LittleFS.open(_currentLogName.c_str(), FILE_APPEND);
    if (!_logFile) return false;

    // If new file, write header
    if (_logFile.size() == 0) {
        _logFile.println("Timestamp,Latitude,Longitude,Speed_kmh,GForce,Satellites,HasFix");
        _logFile.flush();
    }

    _lastFlushTime = millis();
    return true;
}

void StorageManager::rotateIfNeeded() {
    if (!_logFile) return;

    if ((size_t)_logFile.size() >= MAX_LOG_FILE_SIZE) {
        // Start a new file with new timestamp
        _currentLogStartMs = millis();
        openNewLogFile();
    }
}

bool StorageManager::removeLogFile(const char* name) {
    if (!name || strlen(name) == 0) return false;
    String n = name;
    if (!n.startsWith("/")) n = "/" + n;

    // If the file we're trying to remove is currently open, close it first
    if (_logFile) {
        if (_currentLogName.equals(n)) {
            _logFile.close();
            _currentLogName = "";
        }
    }

    // Attempt remove
    if (LittleFS.exists(n)) {
        bool ok = LittleFS.remove(n);
        return ok;
    }
    return false;
}