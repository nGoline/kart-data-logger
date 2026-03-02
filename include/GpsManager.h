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
};

#endif