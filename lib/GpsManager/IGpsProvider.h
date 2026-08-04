#ifndef I_GPS_PROVIDER_H
#define I_GPS_PROVIDER_H

#include <Arduino.h>

class IGpsProvider {
public:
    virtual ~IGpsProvider() = default;

    virtual bool begin() = 0;
    virtual bool update() = 0;
    virtual void end() = 0;

    /* Quiesce the receiver and bring it back. Optional: a provider with no
     * support keeps the defaults and stays fully powered.
     *
     * Note this is not necessarily sleep. On the CASIC part fitted here there
     * is no software power-down at all, so standby() only reduces how much work
     * the receiver does and silences its output. */
    virtual void standby() {}
    virtual void wake() {}

    virtual double getLat() = 0;
    virtual double getLng() = 0;
    virtual double getSpeed(float gForce, float gyroZ) = 0;
    virtual uint32_t getSatellites() = 0;
    virtual uint64_t getEpochMs() = 0;
    virtual bool hasFix() = 0;

    virtual uint16_t getUpdateIntervalMs() const = 0;
};

#endif
