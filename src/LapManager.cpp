#include "LapManager.h"

LapManager::LapManager(GpsManager& gps, AudioManager& audio, FinishLine line)
    : _gps(gps), _audio(audio), _line(line) {}

void LapManager::update() {
    if (!_gps.hasFix()) return;

    double dist = TinyGPSPlus::distanceBetween(
        _gps.getLat(), _gps.getLng(), 
        _line.lat, _line.lng
    );

    uint32_t now = millis();

    // Lógica de Detecção da Linha de Chegada
    // Enter gate: start tracking the minimum distance while inside the gate
    if (dist < _line.radius && (now - _lastLapMillis > MIN_LAP_TIME_MS)) {
        _insideGate = true;
        if (dist < _minDistInGate) {
            _minDistInGate = dist; 
        }
    }
    // Exit gate: require distance to increase sufficiently beyond the min
    // to avoid small GPS oscillations (hysteresis)
    else if (_insideGate && dist > (_minDistInGate + 5.0) && (now - _lastLapMillis > MIN_LAP_TIME_MS)) {
        // Crossed the far side of the gate -> lap complete
        completeLap(now);
    }
}

void LapManager::completeLap(uint32_t now) {
    _lastLapTime = now - _lastLapMillis;
    _lastLapMillis = now;
    _insideGate = false;
    _minDistInGate = 999.9;

    if (_bestLapTime == 0 || _lastLapTime < _bestLapTime) {
        _bestLapTime = _lastLapTime;
    }

    // Parse do tempo para áudio
    int mins = (_lastLapTime / 60000);
    int secs = (_lastLapTime % 60000) / 1000;
    int mms = (_lastLapTime % 1000);

    Serial.printf(">>> VOLTA: %02d:%02d.%03d\n", mins, secs, mms);
    
    // O VoiceParser enfileira os WAVs e o Core 0 toca em background
    VoiceParser::announceLapTime(_audio, mins, secs, mms);
}