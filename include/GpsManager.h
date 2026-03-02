#ifndef GPS_MANAGER_H
#define GPS_MANAGER_H

#include <Arduino.h>
#include <TinyGPS++.h>
#include <string.h>
#include "TelemetryData.h"
#include <string.h>

class GpsManager {
public:
    GpsManager(int8_t rxPin, int8_t txPin);
    
    void begin();
    bool update();

    // starts the internal GPS task that publishes telemetry to queue
    void startTask(QueueHandle_t telemetryQueue, class LapManager* lapTimer, class AudioManager* audio);
    
    double getLat();
    double getLng();
    double getSpeed();
    uint32_t getSatellites();
    bool hasFix();

private:
    int8_t _rxPin;
    int8_t _txPin;
    
    TinyGPSPlus _gps;
    HardwareSerial _serialGps;
    
    void sendUBX(const uint8_t *msg, size_t len);
    void configureUblox();
    
    // Position filter / dead-reckoning state
    double _filteredLat = 0.0;
    double _filteredLng = 0.0;
    unsigned long _lastGpsMillis = 0;
    double _lastBearingDeg = 0.0;
    const double _posAlpha = 0.22; // Complementary filter alpha (gps weight)
    const unsigned long _deadReckonTimeoutMs = 2000; // allow DR up to 2s
    
    // Speed filtering/hysteresis
    double _speedFiltered = 0.0;
    const double _speedAlpha = 0.35; // IIR smoothing for speed
    int _moveCounter = 0;
    const int _moveCountThreshold = 3; // require 3 consecutive updates to consider moving
    const double _minSpeedToMove = 2.0; // km/h - threshold to start moving
    const double _minSpeedToStop = 1.0; // km/h - threshold to stop
    
    double bearingBetween(double lat1, double lon1, double lat2, double lon2);
    void deadReckon(double bearingDeg, double speedKmph, double dtSeconds);
};

#endif