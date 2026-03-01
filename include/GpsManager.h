#ifndef GPS_MANAGER_H
#define GPS_MANAGER_H

#include <Arduino.h>
#include <TinyGPS++.h>

class GpsManager {
public:
    GpsManager(int8_t rxPin, int8_t txPin, uint32_t baud = 9600);
    
    void begin();
    bool update();
    
    // Getters sem o 'const', pois a TinyGPSPlus muta o estado interno na leitura
    double getLat();
    double getLng();
    double getSpeed();
    uint32_t getSatellites();
    double getHdop();
    bool hasFix();

private:
    int8_t _rxPin;
    int8_t _txPin;
    uint32_t _baud;
    
    TinyGPSPlus _gps;
    HardwareSerial _serialGps;
    
    void configureUblox();
    void sendUBX(const uint8_t *msg, size_t len);
};

#endif