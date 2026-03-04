#ifndef GPS_MANAGER_H
#define GPS_MANAGER_H

#include <Arduino.h>
#include <TinyGPS++.h>
#include "EspNowProtocol.h"

class GpsManager {
public:
    GpsManager(int8_t rxPin, int8_t txPin);
    
    void begin();
    bool update();

    double getLat();
    double getLng();
    double getSpeed(float gForce, float gyroZ);
    uint32_t getSatellites();
    uint64_t getEpochMs();
    bool hasFix();

private:
    int8_t _rxPin, _txPin;
    TinyGPSPlus _gps;
    HardwareSerial _serialGps;
    bool _isInitialized = false;

    // Internal U-Blox helpers
    void sendUBX(const uint8_t *msg, size_t len);
    void configureUblox();
    
    // Filtering & Dead Reckoning
    double _filteredLat = 0.0, _filteredLng = 0.0;
    unsigned long _lastGpsMillis = 0;
    double _lastBearingDeg = 0.0;
    const double _posAlpha = 0.22; 
    
    double _speedFiltered = 0.0;
    const double _speedAlpha = 0.35;
    int _moveCounter = 0;

    const double _minSpeedToMove = 2.0; 
    const double _minSpeedToStop = 1.0;
    const int _moveCountThreshold = 3;
    const float IMU_DYN_G_STOP = 0.10f;
    const float IMU_GYROZ_STOP = 3.0f;

    double bearingBetween(double lat1, double lon1, double lat2, double lon2);
    void deadReckon(double bearingDeg, double speedKmph, double dtSeconds);
};

#endif