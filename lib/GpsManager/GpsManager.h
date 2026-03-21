#ifndef GPS_MANAGER_H
#define GPS_MANAGER_H

#include <Arduino.h>
#include <memory>

#include "IGpsProvider.h"

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
    uint16_t getUpdateIntervalMs() const;

private:
    std::unique_ptr<IGpsProvider> _provider;
    static std::unique_ptr<IGpsProvider> createProvider(int8_t rxPin, int8_t txPin);
};

#endif