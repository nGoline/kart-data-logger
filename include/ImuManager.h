#ifndef IMU_MANAGER_H
#define IMU_MANAGER_H

#include "MPU6050.h"
#include <Wire.h>
#include "TelemetryData.h"

struct ImuData {
    float accelX, accelY, accelZ;
    float gyroX, gyroY, gyroZ;
    float gForce;
};

class ImuManager {
public:
    ImuManager();
    bool begin();
    ImuData update();
    void startTask();

private:
    MPU6050 _mpu;
    float _gFiltered = 1.0f;
    const float _gAlpha = 0.12f; // IIR smoothing factor
};

#endif