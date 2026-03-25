#include "BatteryManager.h"
#include "LoggingUtils.h"

BatteryManager::BatteryManager(uint8_t adcPin, uint8_t latchPin) : _pin(adcPin), _latchPin(latchPin) {}

void BatteryManager::begin() {
    pinMode(_pin, ANALOG);
    gpio_pulldown_dis((gpio_num_t)_pin);
    gpio_pullup_dis((gpio_num_t)_pin);
    analogReadResolution(12); // 0-4095
}

float BatteryManager::getVoltage() {
    uint32_t sum = 0;
    const int samples = 16;
    
    for(int i = 0; i < samples; i++) {
        sum += analogRead(_pin);
    }
    
    // 1. Get the real average of the RAW 12-bit units
    float avgRaw = (float)sum / samples; 

    // 2. Convert RAW units to Pin Voltage (0 - 3.3V)
    float pinVoltage = (avgRaw / 4095.0f) * 3.3f;

    // 3. Convert Pin Voltage to actual Battery Voltage (Multiply by 2 for 10k/10k divider)
    float batteryVoltage = pinVoltage * 2.0f;

    // 4. Update your log to match the real variables
    log_v("Raw: %.2f | Pin: %.2fV | Total: %.2fV", avgRaw, pinVoltage, batteryVoltage);
    
    return batteryVoltage;
}

int BatteryManager::getPercentage() {
    float v = getVoltage();
    
    // 1. Define the "Real World" LiPo range
    // Most 1S LiPos are 4.2V (Full) and should not drop below 3.0V (Dead)
    const float V_MAX = 4.1f; // A bit lower to show 100% easier
    const float V_MIN = 3.2f; // The "Safe" floor for our electronics

    // 2. Clamp the values so we don't get 110% or -5%
    if (v >= V_MAX) return 100;
    if (v <= V_MIN) return 0;

    // 3. The Math: (Current - Min) / (Max - Min)
    // For your 3.24V reading: (3.24 - 3.20) / (4.1 - 3.2) = 0.04 / 0.9 = ~4.4%
    float percentage = (v - V_MIN) / (V_MAX - V_MIN) * 100.0f;

    if (percentage < 10.0f) {
        log_w("Battery critically low: %.2fV (%.1f%%)", v, percentage);
    } else if (percentage < 5.0f) {
        LOG_ERROR_FORMATTED("Battery dangerously low: %.2fV (%.1f%%). Initiating shutdown!", v, percentage);
        digitalWrite(_latchPin, LOW); // Release the latch to commit suicide
    }
    
    return (int)percentage;
}