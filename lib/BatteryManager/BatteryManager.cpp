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
        delay(1000);
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
    // Li-ion open-circuit volts at each 10% step, 100% down to 0%. A linear
    // voltage->percent map reads several points low mid-discharge, because the
    // cell sits between 3.7 and 3.8V for most of its usable charge.
    static const uint16_t OCV_MV[] = {4190, 4050, 3990, 3890, 3800,
                                      3720, 3630, 3530, 3420, 3300, 3100};
    const int bands = (int)(sizeof(OCV_MV) / sizeof(OCV_MV[0])) - 1;   // 10 bands of 10%

    int mv = (int)(getVoltage() * 1000.0f);
    if (mv >= OCV_MV[0]) return 100;

    for (int i = 1; i <= bands; i++) {
        if (mv >= OCV_MV[i]) {
            // OCV_MV[i] is the (bands - i) * 10% point; interpolate up into the band above.
            float frac = (float)(mv - OCV_MV[i]) / (float)(OCV_MV[i - 1] - OCV_MV[i]);
            return (int)((bands - i + frac) * 10.0f);
        }
    }
    return 0;   // below 3.10V
}