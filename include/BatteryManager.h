#ifndef BATTERY_MANAGER_H
#define BATTERY_MANAGER_H

#include <Arduino.h>

class BatteryManager {
public:
    BatteryManager(uint8_t pin = 1); // No XIAO ESP32-S3 o divisor de bateria é no GPIO 1
    void begin();
    float getVoltage() const;
    uint8_t getPercentage() const;

private:
    uint8_t _pin;
};

#endif