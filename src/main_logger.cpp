#include <Arduino.h>
#include <Wire.h>
#include <LittleFS.h>

// Modular Libraries
#include "EspNowManager.h"
#include "ImuManager.h"
#include "AudioManager.h"
#include "BatteryManager.h"
#include "LapManager.h"
#include "VoiceParser.h"
#include "EspNowProtocol.h"

#if defined(USE_FAKE_GPS)
#include "FakeGps.h"
#else
#include "GpsManager.h"
#endif

// --- HARDWARE CONFIG ---
#define GPS_RX RX
#define GPS_TX TX
#define I2S_BCLK D1
#define I2S_LRC D2
#define I2S_DIN D0
#define BATT_ADC A9
#define I2C_SDA SDA
#define I2C_SCL SCL

// --- GLOBAL MANAGERS ---
#if defined(USE_FAKE_GPS)
FakeGps fakeGps;
#else
GpsManager gps(GPS_RX, GPS_TX);
#endif
ImuManager imu;
AudioManager audio(I2S_BCLK, I2S_LRC, I2S_DIN);
BatteryManager battery(BATT_ADC, 2.0f);
LapManager lapTimer({-22.772339, -47.139500, 15.0f});

// --- FREERTOS QUEUE & MUTEX ---
QueueHandle_t telemetryQueue;
SemaphoreHandle_t hardwareBusMutex;

// --- TELEMETRY TASK (CORE 1) ---
void telemetryTask(void *pvParameters) {
    log_i("Telemetry Task started on Core 1");
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(200); // 5Hz (Matches GPS rate)

    for (;;) {
        #if defined(USE_FAKE_GPS)
        static TelemetryMsg msg;
        if (fakeGps.update(msg)) {
            log_d("Fake GPS Update");
        } else {
            log_w("Fake GPS failed to update!");
        }

        if (xSemaphoreTake(hardwareBusMutex, portMAX_DELAY) == pdTRUE) {
            xSemaphoreGive(hardwareBusMutex); // Give it back instantly
        }
        #else
        // 1. Update Sensors
        gps.update();
        ImuData imuData = {0}; // Default to 0 in case we can't read

        // --- SAFE IMU READ ---
        // Wait forever (portMAX_DELAY) until the audio task releases the bus
        if (xSemaphoreTake(hardwareBusMutex, portMAX_DELAY) == pdTRUE) {
            imuData = imu.update();
            xSemaphoreGive(hardwareBusMutex); // Give it back instantly
        }

        // 2. We only want to push a message if we have fresh GPS data
        // because the GPS is the limiting factor (5Hz)
        TelemetryMsg msg;
        msg.type = MSG_TELEMETRY;
        msg.speedKmph = gps.getSpeed(imuData.gForce, imuData.gyroZ);
        msg.gForceX = imuData.accelX; // Lateral Gs for cornering
        msg.gForceY = imuData.accelY; // Longitudinal Gs for braking/accel
        msg.totalGForce = imuData.gForce; // Combined G-Force magnitude for the needle
        msg.gyroZ = imuData.gyroZ;
        msg.lat = gps.getLat();
        msg.lng = gps.getLng();
        msg.sats = (uint8_t)gps.getSatellites();
        msg.hasFix = gps.hasFix() ? 1 : 0;
        msg.timestamp = gps.getEpochMs();
        msg.helmetBattery = (uint8_t)battery.getPercentage();
        #endif

        // 3. Send to Queue (Don't block if full, just drop the oldest)
        if (xQueueSend(telemetryQueue, &msg, 0) != pdPASS) {
            log_w("Telemetry Queue Full! Dropping packet.");
        }
        
        #if defined(USE_FAKE_GPS)
        delay(200); // Simulate GPS update delay
        #else
        // Wait for the next cycle
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        #endif
    }
}

void setup() {
#if ARDUINO_USB_CDC_ON_BOOT == 1
    // If the board is configured to use USB CDC on boot, we need to wait for the USB connection to be established before we can use Serial.
    delay(2000);
#endif

    Serial.begin(115200);

    unsigned long serialStart = millis();
    while (!Serial && millis() - serialStart < 2000) { delay(10); }

    // --- INITIALIZE MUTEX FIRST ---
    hardwareBusMutex = xSemaphoreCreateMutex();
    if (hardwareBusMutex == NULL) {
        log_e("Failed to create hardware bus mutex!");
        while(1);
    }

    // 1. Force a "Clean Slate" for I2C Pins
    pinMode(I2C_SCL, OUTPUT);
    for(int i=0; i<9; i++) {
        digitalWrite(I2C_SCL, LOW); delayMicroseconds(5);
        digitalWrite(I2C_SCL, HIGH); delayMicroseconds(5);
    }
    pinMode(I2C_SDA, INPUT_PULLUP);
    pinMode(I2C_SCL, INPUT_PULLUP);
    delay(50); // Reduced delay, 5s is overkill

    if (!Wire.begin(I2C_SDA, I2C_SCL, 100000)) {
        log_e("Failed to initialize I2C on default pins!");
    }

    log_i("--- KART LOGGER BOOTING ---");

    // 2. Initialize IMU FIRST (Silent Calibration)
    log_i("Initializing IMU...");
    bool imuStarted = false;
    if (imu.begin()) { // We removed the custom pins from begin() to use Wire defaults
        log_i("IMU: OK");
        imuStarted = true;
    } else {
        log_e("IMU: Error");
    }

    // 3. Filesystem & Audio
    if (!LittleFS.begin()) {
        log_e("LittleFS Mount Failed!");
        while(1); // Halt if filesystem is dead
    }
    log_i("LittleFS Mounted. Waking up Audio...");
        
    // Start the audio engine on Core 0
    audio.begin(LittleFS, 15); 
    
    #if defined(HAS_STARTUP_AUDIO_CUES)
    audio.tryQueueAudio("/startup.wav");    
    audio.tryQueueAudio("/wait.wav");
    audio.tryQueueAudio("/initializing.wav");
    audio.tryQueueAudio("/silence.wav");
    #endif

    if (!imuStarted) {
        #if defined(HAS_STARTUP_AUDIO_CUES)
        audio.tryQueueAudio("/error.wav");
        audio.tryQueueAudio("/imu.wav");
        audio.tryQueueAudio("/silence.wav");
        #endif

        while(1); // Halt if IMU is dead
    }

    #if defined(HAS_STARTUP_AUDIO_CUES)
    audio.tryQueueAudio("/imu.wav");
    audio.tryQueueAudio("/ready.wav");
    audio.tryQueueAudio("/silence.wav");
    #endif

    // 4. Battery Check
    battery.begin();
    int battLevel = battery.getPercentage();
    log_i("Battery: %d%%", battLevel);
    
    #if defined(HAS_STARTUP_AUDIO_CUES)
    audio.tryQueueAudio("/battery_level.wav");
    VoiceParser::queueNumber(audio, battLevel); // Uncomment if VoiceParser is ready
    audio.tryQueueAudio("/silence.wav");

    // 5. ESP-NOW Radio
    audio.tryQueueAudio("/initializing.wav");
    audio.tryQueueAudio("/radio.wav");
    audio.tryQueueAudio("/silence.wav");
    #endif

    if (EspNowManager::begin()) {
        log_i("Radio: ESP-NOW Broadcast Active.");

        #if defined(HAS_STARTUP_AUDIO_CUES)
        audio.tryQueueAudio("/radio.wav");
        audio.tryQueueAudio("/ready.wav");
        audio.tryQueueAudio("/silence.wav");
        #endif
    }

#ifndef SMOKE_TEST
    // 6. GPS
    #if defined(HAS_STARTUP_AUDIO_CUES)
    audio.tryQueueAudio("/initializing.wav");
    audio.tryQueueAudio("/gps.wav");
    audio.tryQueueAudio("/silence.wav");
    #endif

    #if defined(USE_FAKE_GPS)
    if (!fakeGps.begin("/real_car_ride.csv")) {
        log_e("Failed to start Fake GPS!");
        while(1);
    } else {
        log_i("Fake GPS Initialized with replay file.");
    }
    #else
    if (!gps.begin()) {
        log_e("Failed to start GPS!");
        #if defined(HAS_STARTUP_AUDIO_CUES)
        audio.tryQueueAudio("/error.wav");
        audio.tryQueueAudio("/gps.wav");
        #endif
        while(1);
    }
    #endif

    // 7. Create Telemetry Queue (Holds up to 10 messages)
    telemetryQueue = xQueueCreate(10, sizeof(TelemetryMsg));
    if (telemetryQueue == NULL) {
        log_e("Error creating the queue");
    }

    log_i("Setup Complete. Waiting for boot audio to finish...");
    
    // Hold the CPU here until the playlist is completely empty
    while (audio.isPlaying()) {
        vTaskDelay(pdMS_TO_TICKS(100)); // Feed the watchdog
    }
    
    log_i("Boot sequence finished. Handing over to main loop.");

    // 8. Start Telemetry Task on Core 1
    xTaskCreatePinnedToCore(
        telemetryTask,   // Task function
        "TelemetryTask", // Name of task
        8192,            // Stack size of task
        NULL,            // Parameter of the task
        3,               // Priority of the task (High)
        NULL,            // Task handle to keep track of created task
        1                // Pin task to core 1
    );

#else
    Serial.println("[Main] SMOKE_TEST enabled: GPS task suppressed");
    extern void startSmokeTest();
    startSmokeTest();
#endif
}

void loop() {
#ifndef SMOKE_TEST
    TelemetryMsg msg;

    // Wait for a message in the queue (timeout after 100ms)
    if (xQueueReceive(telemetryQueue, &msg, pdMS_TO_TICKS(100)) == pdPASS) {
        
        // 1. ALWAYS Send to CYD (So the G-Force needle and Sat count work in the pits!)
        EspNowManager::sendTelemetry(msg);

        // --- THE GPS LOCK STATE MACHINE ---
        static bool isSystemReady = false;
        static int lastSats = -1;
        static uint32_t lastAnnounceTime = 0;

        if (!isSystemReady) {
            // STATE: SEARCHING FOR FIX
            if (msg.hasFix) {
                // FIX ACQUIRED! Transition to Running State.
                isSystemReady = true;
                log_i("GPS Lock Acquired! (%d Sats). System Ready.", msg.sats);
                
                #if defined(HAS_STARTUP_AUDIO_CUES)
                audio.tryQueueAudio("/gps.wav");
                audio.tryQueueAudio("/ready.wav");
                audio.tryQueueAudio("/silence.wav");
                audio.tryQueueAudio("/system_ready.wav");
                #endif
            } else {
                // STILL SEARCHING: Announce satellites if the count changed 
                // AND at least 30 seconds have passed since the last announcement.
                if ((millis() - lastAnnounceTime > 30000)) { //msg.sats != lastSats && 
                    log_i("Searching... Satellites: %d", msg.sats);
                    
                    #if defined(HAS_STARTUP_AUDIO_CUES)
                    audio.tryQueueAudio("/wait.wav");
                    VoiceParser::queueNumber(audio, msg.sats);
                    audio.tryQueueAudio("/gps.wav");
                    audio.tryQueueAudio("/silence.wav");
                    #endif
                    
                    lastSats = msg.sats;
                    lastAnnounceTime = millis();
                }
            }
            
            // Return early! This drops the raw telemetry data and prevents 
            // ESP-NOW broadcasting and lap timing until we have a real fix.
            return; 
        }

        // ==========================================
        // STATE: SYSTEM READY (NORMAL RUNNING)
        // ==========================================

        // 2. Check for Lap Completion
        if (lapTimer.processTelemetry(msg)) {
            uint32_t lt = lapTimer.getLastLapTime();
            uint32_t bt = lapTimer.getBestLapTime();
            uint32_t pt = lapTimer.getPreviousLapTime();
            int minutes = lt / 60000;
            int seconds = (lt % 60000) / 1000;
            int millis = lt % 1000;
            bool isBest = (lt == bt && bt != 0);

            // --- ANNOUNCE LAP + DELTA ---
            VoiceParser::announceLapTime(audio, minutes, seconds, millis, isBest);

            if (pt > 0) { // Don't announce a delta on the first lap   
                // Calculate Delta (Difference from previous lap)
                // We use the absolute value for the number, then add the direction
                int32_t delta = (int32_t)lt - (int32_t)pt;
                int deltaMinutes = abs(delta / 60000);
                int deltaSeconds = abs((delta % 60000) / 1000);
                int deltaMillis = abs(delta % 1000);

                if (deltaMinutes > 0 || deltaSeconds > 0 || deltaMillis > 0) {
                    audio.tryQueueAudio("/silence.wav");
                    VoiceParser::announceDeltaTime(audio, deltaMinutes, deltaSeconds, deltaMillis, delta <= 0);
                }
            }
        }

        // Debug Log
        log_d("SENT: Spd:%.1f G:%.2f Sats:%d Fix:%d | Batt: %.2f%%", 
              msg.speedKmph, msg.gForce, msg.sats, msg.hasFix, battery.getPercentage());
    }
#endif

    // Allow background tasks to breathe
    vTaskDelay(pdMS_TO_TICKS(10));
}