#ifndef BATTERY_MANAGER_H
#define BATTERY_MANAGER_H

#include <Arduino.h>

class BatteryManager {
public:
    // adcPin: The GPIO used for sensing
    // dividerEnablePin: GPIO that enables the voltage divider only while measuring.
    //   Pass 0xFF if the divider is always-on (no enable pin needed).
    // dividerRatio: V_BAT / V_PIN = (R_top + R_bot) / R_bot
    BatteryManager(uint8_t adcPin, uint8_t dividerEnablePin, float dividerRatio = 2.0f);

    void begin();
    float getVoltage();
    int getPercentage(); // 0-100%

private:
    uint8_t _pin;
    uint8_t _dividerEnablePin;
    float _dividerRatio;
};

#endif