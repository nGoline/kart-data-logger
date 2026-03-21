#ifndef ATGM336_GPS_PROVIDER_H
#define ATGM336_GPS_PROVIDER_H

#include <Arduino.h>
#include <TinyGPS++.h>

#include "../IGpsProvider.h"

class Atgm336GpsProvider : public IGpsProvider {
public:
    static constexpr uint16_t kUpdateIntervalMs = 100;

    Atgm336GpsProvider(int8_t rxPin, int8_t txPin);

    bool begin() override;
    bool update() override;

    double getLat() override;
    double getLng() override;
    double getSpeed(float gForce, float gyroZ) override;
    uint32_t getSatellites() override;
    uint64_t getEpochMs() override;
    bool hasFix() override;
    uint16_t getUpdateIntervalMs() const override;

private:
    int8_t _rxPin;
    int8_t _txPin;
    TinyGPSPlus _gps;
    HardwareSerial _serialGps;

    uint32_t _currentBaud = 115200;

    void sendPCAS(char* msgID, char* payload);
    void configureAtgm336(bool hasPendingConfigChanges);

    double _speedFiltered = 0.0;
    const double _speedAlpha = 0.35;
    int _moveCounter = 0;

    const double _minSpeedToMove = 2.0;
    const double _minSpeedToStop = 1.0;
    const int _moveCountThreshold = 3;
    const float _imuDynGStop = 0.08f;
    const float _imuGyroZStop = 3.5f;
};

#endif
