#ifndef GPS_MANAGER_H
#define GPS_MANAGER_H

#include <Arduino.h>
#include <TinyGPS++.h>
#include "EspNowProtocol.h"

struct UBXConfig {
    uint8_t dynModel;     // 4 = Automotive, 3 = Pedestrian, etc.
    uint16_t measRate;    // 200 = 5Hz, 1000 = 1Hz
    uint32_t baudRate;    // 115200, 9600, etc.
    uint8_t perfMode;     // 0 = Max Performance, 1 = Power Save, 4 = Eco.
};

class GpsManager {
public:
    GpsManager(int8_t rxPin, int8_t txPin);
    
    bool begin();
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
    void sendUBXWithChecksum(uint8_t msgClass, uint8_t msgID, uint8_t* payload, uint16_t len);
    void configureUblox();
    UBXConfig readCurrentConfig();
    bool pollUBX(uint8_t msgClass, uint8_t msgID, uint8_t* payload, uint8_t payloadLen);
    void checkFirmware();

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