#ifndef IMU_MANAGER_H
#define IMU_MANAGER_H

#include <Arduino.h>
#include <I2Cdev.h>
#include <MPU6050.h>
#include <Wire.h>
#include "EspNowProtocol.h" // For TelemetryMsg

struct ImuData {
    float accelX, accelY, accelZ;
    float gyroX, gyroY, gyroZ;
    float gForce;
};

class ImuManager {
public:
    ImuManager();
    bool begin(int sda = -1, int scl = -1); // Support custom I2C pins
    ImuData update();

private:
    MPU6050 _mpu;
    float _gFiltered = 1.0f;
    const float _gAlpha = 0.12f;
    int _sda_pin, _scl_pin;
};

#endif