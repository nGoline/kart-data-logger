#ifndef I_GPS_PROVIDER_H
#define I_GPS_PROVIDER_H

#include <Arduino.h>

class IGpsProvider {
public:
    virtual ~IGpsProvider() = default;

    virtual bool begin() = 0;
    virtual bool update() = 0;

    virtual double getLat() = 0;
    virtual double getLng() = 0;
    virtual double getSpeed(float gForce, float gyroZ) = 0;
    virtual uint32_t getSatellites() = 0;
    virtual uint64_t getEpochMs() = 0;
    virtual bool hasFix() = 0;

    virtual uint16_t getUpdateIntervalMs() const = 0;
};

#endif
