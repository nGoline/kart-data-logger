#include "PowerManager.h"
#include "LoggingUtils.h"
#include "AudioManager.h"
#if !defined(USE_FAKE_GPS)
#include "GpsManager.h"
#endif
#include <esp_sleep.h>

#if !defined(USE_FAKE_GPS)
extern GpsManager gps;
#endif
extern AudioManager audio;

PowerManager::PowerManager(uint8_t offButtonPin, uint8_t peripheralEnablePin, uint32_t holdTimeMs, uint8_t minBatteryPercent)
    : _offButtonPin(offButtonPin)
    , _peripheralEnablePin(peripheralEnablePin)
    , _holdTimeMs(holdTimeMs)
    , _minBatteryPercent(minBatteryPercent)
    , _buttonPressTime(0)
    , _isButtonPressed(false)
    , _ignoreInitialBootPress(true) {}

void PowerManager::begin() {
    pinMode(_peripheralEnablePin, OUTPUT);
    digitalWrite(_peripheralEnablePin, HIGH);

    pinMode(_offButtonPin, INPUT_PULLUP);
}

void PowerManager::update(int batteryPercentage) {
    if (batteryPercentage < 5.0f) {
        log_w("Battery critically low: %.1f%%", batteryPercentage);
    } else if (batteryPercentage <= _minBatteryPercent) {
        log_w("Battery below threshold: %d%%. Entering hibernate.", batteryPercentage);
        enterHibernate("low battery");
    }

    bool currentButtonState = (digitalRead(_offButtonPin) == LOW);
    if (currentButtonState && !_isButtonPressed) {
        log_i("Off button pressed.");
        _buttonPressTime = millis();
        _isButtonPressed = true;
    } else if (!currentButtonState && _isButtonPressed) {
        log_i("Off button released.");
        _isButtonPressed = false;
        _ignoreInitialBootPress = false;
    }

    if (_isButtonPressed && !_ignoreInitialBootPress && (millis() - _buttonPressTime >= _holdTimeMs)) {
        log_w("Manual shutdown held for %lu ms.", (unsigned long)(millis() - _buttonPressTime));
        while (digitalRead(_offButtonPin) == LOW) {
            digitalWrite(LED_BUILTIN, LOW);
            delay(50);
        }
        enterHibernate("power button");
    }
}

void PowerManager::enterHibernate(const char *reason) {
    log_i("PowerManager entering hibernate: %s", reason);

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    // Deactivate all peripherals to minimize power draw in sleep
    #if !defined(USE_FAKE_GPS)
    log_i("Shutting down GPS...");
    gps.end();
    #endif

    log_i("Shutting down audio...");
    audio.end();

    delay(100); // Ensure all peripherals have time to power down
    // Ensure the peripheral enable pin is LOW to cut power to sensors
    digitalWrite(_peripheralEnablePin, LOW);

    // Configure Wake-up Sources
    esp_sleep_enable_ext0_wakeup((gpio_num_t)_offButtonPin, LOW);

    // This will consume about 16uA
    esp_deep_sleep_start();
}
