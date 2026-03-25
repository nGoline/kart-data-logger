#ifndef UBLOX_GPS_PROVIDER_H
#define UBLOX_GPS_PROVIDER_H

#include <Arduino.h>
#include <TinyGPS++.h>
#include "LoggingUtils.h"

#include "../IGpsProvider.h"

struct UBXConfig {
    uint8_t dynModel;
    uint16_t measRate;
    uint8_t perfMode;
};

class UbloxGpsProvider : public IGpsProvider {
public:
    static constexpr uint16_t kUpdateIntervalMs = 200;

    UbloxGpsProvider(int8_t rxPin, int8_t txPin);

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

    void sendUBXWithChecksum(uint8_t msgClass, uint8_t msgID, uint8_t* payload, uint16_t len);
    void configureUblox();
    UBXConfig readCurrentConfig();
    bool pollUBX(uint8_t msgClass, uint8_t msgID, uint8_t* payload, uint8_t payloadLen);

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
