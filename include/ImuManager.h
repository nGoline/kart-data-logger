#ifndef IMU_MANAGER_H
#define IMU_MANAGER_H

#include "MPU6050.h"
#include <Wire.h>

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

private:
    MPU6050 _mpu;
};

#endif