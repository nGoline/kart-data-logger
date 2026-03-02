#ifndef LAP_MANAGER_H
#define LAP_MANAGER_H

#include <Arduino.h>
#include "GpsManager.h"
#include "AudioManager.h"
#include "VoiceParser.h"
#include "Config.h"

struct FinishLine {
    double lat;
    double lng;
    float radius; // metros
};

class LapManager {
public:
    LapManager(GpsManager& gps, AudioManager& audio, FinishLine line);
    
    void update();
    uint32_t getLastLapTime() const { return _lastLapTime; }
    uint32_t getBestLapTime() const { return _bestLapTime; }

private:
    GpsManager& _gps;
    AudioManager& _audio;
    FinishLine _line;

    uint32_t _lastLapMillis = 0;
    uint32_t _lastLapTime = 0;
    uint32_t _bestLapTime = 0;
    
    bool _insideGate = false;
    double _minDistInGate = 999.9;
    const uint32_t MIN_LAP_TIME_MS = 5000; // minimum 5s between laps to avoid false positives

    void completeLap(uint32_t now);
};

#endif