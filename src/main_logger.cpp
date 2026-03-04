#include <Arduino.h>
#include <Wire.h>
#include <LittleFS.h>

// Modular Libraries
#include "EspNowManager.h"
#include "GpsManager.h"
#include "ImuManager.h"
#include "AudioManager.h"
#include "BatteryManager.h"
#include "LapManager.h"
#include "VoiceParser.h"
#include "EspNowProtocol.h"

// --- HARDWARE CONFIG ---
// GPS Pins (Update these based on your specific ESP32 board)
#define GPS_RX D7
#define GPS_TX D6
// I2S Pins
#define I2S_BCLK D0
#define I2S_LRC D1
#define I2S_DIN D2
// Battery Pin
#define BATT_ADC A9
// I2C Pins (if using non-default)
#define I2C_SDA SDA
#define I2C_SCL SCL

// --- GLOBAL MANAGERS ---
GpsManager gps(GPS_RX, GPS_TX);
ImuManager imu;
AudioManager audio(I2S_BCLK, I2S_LRC, I2S_DIN);
BatteryManager battery(BATT_ADC, 2.0f); // Assuming 10k/10k divider
LapManager lapTimer({-23.5505, -46.6333, 15.0f}); // Track Finish Line

void setup() {
    Serial.begin(115200);

    // 1. Force a "Clean Slate" for I2C Pins
    pinMode(I2C_SCL, OUTPUT); // SCL
    for(int i=0; i<9; i++) {
        digitalWrite(I2C_SCL, LOW); delayMicroseconds(5);
        digitalWrite(I2C_SCL, HIGH); delayMicroseconds(5);
    }
    pinMode(I2C_SDA, INPUT_PULLUP);
    pinMode(I2C_SCL, INPUT_PULLUP);
    delay(5000);
    if (!Wire.begin(I2C_SDA, I2C_SCL, 100000)) { // Default I2C pins with 100kHz
        log_e("Failed to initialize I2C on default pins!");
    }

    log_i("--- KART LOGGER BOOTING ---");

    // 1. Filesystem & Audio (Critical for startup cues)
    if (!LittleFS.begin()) {
        log_e("LittleFS Mount Failed!");
    }
    delay(1000);
    
    // Begin Audio (Passing LittleFS reference as we refactored)
    // audio.begin(LittleFS, 15); 
    // audio.queueAudio("/wait.wav");
    // audio.queueAudio("/initializing.wav");
    delay(1000);

    // 2. Battery Check (Before WiFi/Radio starts to avoid noise)
    battery.begin();
    int battLevel = battery.getPercentage();
    log_i("Battery: %d%%", battLevel);
    delay(10);
    
    // Queue Battery Audio Cues
    // audio.queueAudio("/battery_level.wav");
    // VoiceParser::queueNumber(audio, battLevel);
    // audio.queueAudio("/percent.wav");
    // delay(2000);

    // 3. ESP-NOW Radio
    if (EspNowManager::begin()) {
        log_i("Radio: ESP-NOW Broadcast Active.");
    }
    delay(10);

#ifndef SMOKE_TEST
    // 4. GPS
    gps.begin();
    delay(10);

    // 5. IMU
    log_i("Initializing IMU...");
    if (imu.begin(D3, D4)) {
        log_i("IMU: OK");
        // audio.queueAudio("/imu.wav");
        // audio.queueAudio("/ready.wav");
    } else {
        log_e("IMU: Error");
        // audio.queueAudio("/error.wav");
        // audio.queueAudio("/imu.wav");
    }
    delay(10);
#else
    // When SMOKE_TEST is enabled we don't start the real GPS task (avoids GPS audio cues)
    Serial.println("[Main] SMOKE_TEST enabled: GPS task suppressed");
    extern void startSmokeTest();

    startSmokeTest();
#endif

    // audio.queueAudio("/system_ready.wav");
    log_i("Setup Complete.");
}

void loop() {
    #ifndef SMOKE_TEST
    // 1. Update Sensors
    bool newGPS = gps.update();
    ImuData imuData = imu.update();

    // 2. Process and Send Telemetry (at GPS rate: 5Hz)
    if (newGPS) {
        TelemetryMsg msg;
        msg.type = MSG_TELEMETRY;
        msg.speedKmph = gps.getSpeed(imuData.gForce, imuData.gyroZ);
        msg.gForce = imuData.gForce;
        msg.gyroZ = imuData.gyroZ;
        msg.lat = gps.getLat();
        msg.lng = gps.getLng();
        msg.sats = (uint8_t)gps.getSatellites();
        msg.hasFix = gps.hasFix() ? 1 : 0;
        msg.timestamp = gps.getEpochMs();

        // 3. Send to CYD Display
        EspNowManager::sendTelemetry(msg);

        // 4. Check for Lap Completion
        if (lapTimer.processTelemetry(msg)) {
            uint32_t lt = lapTimer.getLastLapTime();
            int mins = lt / 60000;
            int secs = (lt % 60000) / 1000;
            int mms = lt % 1000;
            VoiceParser::announceLapTime(audio, mins, secs, mms);
        }

        // Debug Log (Uses ardhal formatting)
        log_d("SENT: Spd:%.1f G:%.2f Sats:%d Fix:%d | Batt: %.2f%%", 
              msg.speedKmph, msg.gForce, msg.sats, msg.hasFix, battery.getPercentage());
    }
    #endif

    // Allow background tasks (Audio/Watchdog) to breathe
    vTaskDelay(pdMS_TO_TICKS(1));
}