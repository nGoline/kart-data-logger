#include "BatteryManager.h"
#include "LoggingUtils.h"

BatteryManager::BatteryManager(uint8_t adcPin, uint8_t dividerEnablePin, float dividerRatio)
    : _pin(adcPin)
    , _dividerEnablePin(dividerEnablePin)
    , _dividerRatio(dividerRatio) {}

void BatteryManager::begin() {
    if (_dividerEnablePin != 0xFF) {
        pinMode(_dividerEnablePin, OUTPUT);
        digitalWrite(_dividerEnablePin, LOW);
    }
    pinMode(_pin, ANALOG);
    gpio_pulldown_dis((gpio_num_t)_pin);
    gpio_pullup_dis((gpio_num_t)_pin);
}

float BatteryManager::getVoltage() {
    if (_dividerEnablePin != 0xFF) {
        digitalWrite(_dividerEnablePin, HIGH);
        delay(5);
    }

    uint32_t sumMv = 0;
    const int samples = 16;

    for (int i = 0; i < samples; i++) {
        sumMv += analogReadMilliVolts(_pin);
    }

    if (_dividerEnablePin != 0xFF) {
        digitalWrite(_dividerEnablePin, LOW);
    }

    // analogReadMilliVolts uses the ESP32 factory calibration curve,
    // which corrects the non-linearity that makes raw analogRead inaccurate above ~2.5V.
    float pinVoltage = (float)sumMv / samples / 1000.0f;
    float batteryVoltage = pinVoltage * _dividerRatio;

    log_v("Pin: %.2fV | Total: %.2fV", pinVoltage, batteryVoltage);
    
    return batteryVoltage;
}

int BatteryManager::getPercentage() {
    float v = getVoltage();
    
    // 1. Define the "Real World" LiPo range
    // Most 1S LiPos are 4.2V (Full) and should not drop below 3.0V (Dead)
    const float V_MAX = 4.2f; // A bit lower to show 100% easier
    const float V_MIN = 3.2f; // The "Safe" floor for our electronics

    // 2. Clamp the values so we don't get 110% or -5%
    if (v >= V_MAX) return 100;
    if (v <= V_MIN) return 0;

    // 3. The Math: (Current - Min) / (Max - Min)
    // For your 3.24V reading: (3.24 - 3.20) / (4.1 - 3.2) = 0.04 / 0.9 = ~4.4%
    float percentage = (v - V_MIN) / (V_MAX - V_MIN) * 100.0f;
    
    return (int)percentage;
}