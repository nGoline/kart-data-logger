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
#define GPS_RX RX
#define GPS_TX TX
#define I2S_BCLK D1
#define I2S_LRC D2
#define I2S_DIN D0
#define BATT_ADC A9
#define I2C_SDA SDA
#define I2C_SCL SCL

// --- GLOBAL MANAGERS ---
GpsManager gps(GPS_RX, GPS_TX);
ImuManager imu;
AudioManager audio(I2S_BCLK, I2S_LRC, I2S_DIN);
BatteryManager battery(BATT_ADC, 2.0f);
LapManager lapTimer({-23.5505, -46.6333, 15.0f});

// --- FREERTOS QUEUE & MUTEX ---
QueueHandle_t telemetryQueue;
SemaphoreHandle_t hardwareBusMutex;

// --- TELEMETRY TASK (CORE 1) ---
void telemetryTask(void *pvParameters) {
    log_i("Telemetry Task started on Core 1");
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(200); // 5Hz (Matches GPS rate)

    for (;;) {
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
        msg.gForce = imuData.gForce;
        msg.gyroZ = imuData.gyroZ;
        msg.lat = gps.getLat();
        msg.lng = gps.getLng();
        msg.sats = (uint8_t)gps.getSatellites();
        msg.hasFix = gps.hasFix() ? 1 : 0;
        msg.timestamp = gps.getEpochMs();

        // 3. Send to Queue (Don't block if full, just drop the oldest)
        if (xQueueSend(telemetryQueue, &msg, 0) != pdPASS) {
            log_w("Telemetry Queue Full! Dropping packet.");
        }
        
        // Wait for the next cycle
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void setup() {
    Serial.begin(115200);

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
    
    audio.queueAudio("/wait.wav");
    audio.queueAudio("/initializing.wav");
    audio.queueAudio("/silence.wav");

    audio.queueAudio("/file_system.wav");
    audio.queueAudio("/ready.wav");
    audio.queueAudio("/silence.wav");

    if (imuStarted) {
        audio.queueAudio("/imu.wav");
        audio.queueAudio("/ready.wav");
        audio.queueAudio("/silence.wav");
    } else {
        audio.queueAudio("/error.wav");
        audio.queueAudio("/imu.wav");
        audio.queueAudio("/silence.wav");

        while(1); // Halt if IMU is dead
    }

    // 4. Battery Check
    battery.begin();
    int battLevel = battery.getPercentage();
    log_i("Battery: %d%%", battLevel);
    
    audio.queueAudio("/battery_level.wav");
    VoiceParser::queueNumber(audio, battLevel); // Uncomment if VoiceParser is ready
    audio.queueAudio("/silence.wav");

    // 5. ESP-NOW Radio
    audio.queueAudio("/initializing.wav");
    audio.queueAudio("/radio.wav");
    audio.queueAudio("/silence.wav");

    if (EspNowManager::begin()) {
        log_i("Radio: ESP-NOW Broadcast Active.");

        audio.queueAudio("/radio.wav");
        audio.queueAudio("/ready.wav");
        audio.queueAudio("/silence.wav");
    }

#ifndef SMOKE_TEST
    // 6. GPS
    audio.queueAudio("/initializing.wav");
    audio.queueAudio("/gps.wav");
    audio.queueAudio("/silence.wav");
    gps.begin();

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
                
                audio.queueAudio("/gps.wav");
                audio.queueAudio("/ready.wav");
                audio.queueAudio("/silence.wav");
                audio.queueAudio("/system_ready.wav");
                
            } else {
                // STILL SEARCHING: Announce satellites if the count changed 
                // AND at least 30 seconds have passed since the last announcement.
                if ((millis() - lastAnnounceTime > 30000)) { //msg.sats != lastSats && 
                    log_i("Searching... Satellites: %d", msg.sats);
                    
                    audio.queueAudio("/wait.wav");
                    VoiceParser::queueNumber(audio, msg.sats);
                    audio.queueAudio("/gps.wav");
                    audio.queueAudio("/silence.wav");
                    
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
            int mins = lt / 60000;
            int secs = (lt % 60000) / 1000;
            int mms = lt % 1000;
            // VoiceParser::announceLapTime(audio, mins, secs, mms);
        }

        // Debug Log
        log_i("SENT: Spd:%.1f G:%.2f Sats:%d Fix:%d | Batt: %.2f%%", 
              msg.speedKmph, msg.gForce, msg.sats, msg.hasFix, battery.getPercentage());
    }
#endif

    // Allow background tasks to breathe
    vTaskDelay(pdMS_TO_TICKS(10));
}