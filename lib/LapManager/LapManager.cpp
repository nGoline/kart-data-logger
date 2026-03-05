#include "LapManager.h"
#include <TinyGPS++.h>

LapManager::LapManager(FinishLine line) : _line(line) {
    log_i("LapManager: Finish line set at %.6f, %.6f (Radius: %.1fm)", _line.lat, _line.lng, _line.radius);
}

bool LapManager::processTelemetry(const TelemetryMsg& data) {
    if (!data.hasFix) return false;

    // Use the static math helper from TinyGPS++
    double dist = TinyGPSPlus::distanceBetween(
        data.lat, data.lng, 
        _line.lat, _line.lng
    );

    uint32_t now = millis();
    bool lapJustFinished = false;

    // 1. Entry Logic
    if (dist < _line.radius) {
        if (!_insideGate && (now - _lastLapMillis > MIN_LAP_TIME_MS)) {
            _insideGate = true;
            _minDistInGate = dist;
            log_d("LapManager: Entered finish gate. Dist: %.2fm", dist);
        }
        
        // Track the "Apex" of the finish line crossing
        if (_insideGate && dist < _minDistInGate) {
            _minDistInGate = dist;
        }
    } 
    // 2. Exit Logic (Crossed the line)
    else if (_insideGate) {
        // Once we are outside the radius again, we complete the lap
        completeLap(now);
        lapJustFinished = true;
    }

    return lapJustFinished;
}

void LapManager::completeLap(uint32_t now) {
    // Prevent first "lap" from being the time since boot
    if (_lastLapMillis == 0) {
        _lastLapMillis = now;
        _insideGate = false;
        log_i("LapTimer: Session Started.");
        return;
    }

    uint32_t lapMeasured = now - _lastLapMillis;

    // Calculate delta against previous lap BEFORE updating _lastLapTime
    if (_lastLapTime > 0) {
        _prevLapTime = _lastLapTime;
        _deltaLast = (int32_t)lapMeasured - (int32_t)_prevLapTime;
    }

    _lastLapTime = lapMeasured;
    _lastLapMillis = now;
    _insideGate = false;
    _minDistInGate = 999.9;

    if (_bestLapTime == 0 || _lastLapTime < _bestLapTime) {
        _bestLapTime = _lastLapTime;
        log_i("NEW BEST LAP!");
    }

    int mins = (_lastLapTime / 60000);
    int secs = (_lastLapTime % 60000) / 1000;
    int mms = (_lastLapTime % 1000);

    log_i("LAP COMPLETED: %02d:%02d.%03d", mins, secs, mms);
}