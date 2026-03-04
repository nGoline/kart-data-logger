#ifndef LAP_MANAGER_H
#define LAP_MANAGER_H

#include <Arduino.h>
#include "EspNowProtocol.h" // For TelemetryMsg/Data
#include "AudioManager.h"

struct FinishLine {
    double lat;
    double lng;
    float radius; // in meters
};

class LapManager {
public:
    LapManager(FinishLine line);
    
    // Returns true if a lap was just completed
    bool processTelemetry(const TelemetryMsg& data);

    uint32_t getLastLapTime() const { return _lastLapTime; }
    uint32_t getBestLapTime() const { return _bestLapTime; }

private:
    FinishLine _line;
    uint32_t _lastLapMillis = 0;
    uint32_t _lastLapTime = 0;
    uint32_t _bestLapTime = 0;
    
    bool _insideGate = false;
    double _minDistInGate = 999.9;
    const uint32_t MIN_LAP_TIME_MS = 15000; // Increased to 15s for karts

    void completeLap(uint32_t now);
};

#endif