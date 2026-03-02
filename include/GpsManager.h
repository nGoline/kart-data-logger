#ifndef GPS_MANAGER_H
#define GPS_MANAGER_H

#include <Arduino.h>
#include <TinyGPS++.h>

class GpsManager {
public:
    GpsManager(int8_t rxPin, int8_t txPin);
    
    void begin();
    bool update();
    
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
};

#endif