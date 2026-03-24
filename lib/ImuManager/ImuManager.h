#ifndef IMU_MANAGER_H
#define IMU_MANAGER_H

#include <Arduino.h>

// ImuData is always defined so telemetry code can zero-initialise it
// even when ENABLE_IMU is not set.
struct ImuData {
    float accelX, accelY, accelZ;
    float gyroX, gyroY, gyroZ;
    float gForce;
};

#if defined(ENABLE_IMU)

#include <Wire.h>
#include <I2Cdev.h>
#include <MPU6050.h>

class ImuManager {
public:
    ImuManager();
    bool begin(int sda = -1, int scl = -1, uint32_t i2cFrequency = 100000, bool recoverBusOnBegin = true);
    ImuData update();

private:
    bool restartI2cBus();
    void recoverStuckBus();

    MPU6050 _mpu;
    float _gFiltered = 1.0f;
    const float _gAlpha = 0.12f;
    int _sda_pin = -1;
    int _scl_pin = -1;
    uint32_t _i2cFrequency = 100000;
    bool _recoverBusOnBegin = true;
};

#endif // ENABLE_IMU

#endif // IMU_MANAGER_H