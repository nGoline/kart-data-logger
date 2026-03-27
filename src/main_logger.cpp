#include <Arduino.h>
#include <LittleFS.h>

// Modular Libraries
#include "EspNowManager.h"
#include "AudioManager.h"
#include "BatteryManager.h"
#include "LapManager.h"
#include "VoiceParser.h"
#include "ErrorLogManager.h"
#include "EspNowProtocol.h"
#include "LoggingUtils.h"

#if defined(USE_FAKE_GPS)
#include "FakeGps.h"
#else
#include "GpsManager.h"
#endif

// --- HARDWARE CONFIG ---
#define I2S_DIN D0
#define I2S_BCLK D1
#define I2S_LRC D2
#define LATCH_PIN D3
#define I2C_SDA SDA
#define I2C_SCL SCL
#define GPS_RX RX
#define GPS_TX TX
#define OFF_BUTTON_PIN D8
#define BATT_ADC A9
#define HOLD_TIME_MS 3000 // 3 seconds to turn off
#define CAPACITOR_DISCHARGE_TIME_MS 5000 // Time to wait for the capacitor to discharge before cutting power

// --- GLOBAL MANAGERS ---
#if defined(USE_FAKE_GPS)
FakeGps fakeGps;
#else
GpsManager gps(GPS_RX, GPS_TX);
#endif

AudioManager audio(I2S_BCLK, I2S_LRC, I2S_DIN);
BatteryManager battery(BATT_ADC, 2.0f);
LapManager lapTimer;
ErrorLogManager errorLogger;

// --- FREERTOS QUEUE & MUTEX ---
QueueHandle_t telemetryQueue;

#if !defined(USE_FAKE_GPS)
// Wait for a newer IMU sample than lastUsedCounter. We keep a strict timeout so
// telemetry cadence remains stable even if a packet is dropped.
static bool waitForFreshImuSample(uint32_t &lastUsedCounter, ImuFeedbackMsg &imuOut, TickType_t maxWaitTicks) {
    TickType_t startTick = xTaskGetTickCount();

    while ((xTaskGetTickCount() - startTick) < maxWaitTicks) {
        uint32_t counter = 0;
        if (EspNowManager::getLatestImuFeedback(imuOut, counter) && counter > lastUsedCounter) {
            lastUsedCounter = counter;
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    // Fallback to the latest known sample if a fresh one did not arrive in time.
    uint32_t counter = 0;
    if (EspNowManager::getLatestImuFeedback(imuOut, counter)) {
        lastUsedCounter = counter;
        return false;
    }

    imuOut = {};
    imuOut.type = MSG_IMU_FEEDBACK;
    return false;
}

static void monitorGpsDataFlow(bool gotFreshGpsSentence) {
    static uint32_t lastGoodGpsSentenceMs = 0;
    static uint32_t lastGpsTimeoutLogMs = 0;
    static bool gpsWasTimedOut = false;

    const uint32_t now = millis();
    const uint32_t startupGraceMs = 5000;
    const uint32_t gpsTimeoutMs = 3000;
    const uint32_t repeatLogMs = 30000;

    if (gotFreshGpsSentence) {
        lastGoodGpsSentenceMs = now;
        if (gpsWasTimedOut) {
            log_i("GPS data stream recovered.");
            gpsWasTimedOut = false;
        }
        return;
    }

    if (now < startupGraceMs) {
        return;
    }

    if (lastGoodGpsSentenceMs == 0) {
        if (now - lastGpsTimeoutLogMs >= repeatLogMs) {
            LOG_ERROR("GPS data timeout: no valid sentences received since boot.");
            lastGpsTimeoutLogMs = now;
            gpsWasTimedOut = true;
        }
        return;
    }

    if ((now - lastGoodGpsSentenceMs) >= gpsTimeoutMs && (now - lastGpsTimeoutLogMs) >= repeatLogMs) {
        LOG_ERROR_FORMATTED("GPS data timeout: no valid sentence for %lu ms.", (unsigned long)(now - lastGoodGpsSentenceMs));
        lastGpsTimeoutLogMs = now;
        gpsWasTimedOut = true;
    }
}
#endif

// --- TELEMETRY TASK (CORE 1) ---
void telemetryTask(void *pvParameters) {
    log_i("Telemetry Task started on Core 1");
    TickType_t xLastWakeTime = xTaskGetTickCount();
    #if defined(USE_FAKE_GPS)
    const TickType_t xFrequency = pdMS_TO_TICKS(200);
    #else
    const TickType_t xFrequency = pdMS_TO_TICKS(gps.getUpdateIntervalMs());
    #endif

#if !defined(USE_FAKE_GPS)
    uint32_t lastImuCounterUsed = 0;
    const TickType_t imuWaitBudget = (xFrequency > pdMS_TO_TICKS(10))
        ? (xFrequency - pdMS_TO_TICKS(10))
        : pdMS_TO_TICKS(0);
#endif

    for (;;) {
        #if defined(USE_FAKE_GPS)
        static TelemetryMsg msg;
        if (fakeGps.update(msg)) {
            log_d("Fake GPS Update: Epoch: %llu", msg.timestamp);
        } else {
            log_w("Fake GPS failed to update!");
        }

        #else
        // 1. Update Sensors
        bool gotFreshGpsSentence = gps.update();
        monitorGpsDataFlow(gotFreshGpsSentence);
        ImuFeedbackMsg imuFeedback = {};
        bool usedFreshImu = waitForFreshImuSample(lastImuCounterUsed, imuFeedback, imuWaitBudget);

        static uint32_t lastImuStaleWarnMs = 0;
        if (!usedFreshImu && (millis() - lastImuStaleWarnMs > 1000)) {
            log_w("IMU uplink stale; reusing latest sample for this frame.");
            lastImuStaleWarnMs = millis();
        }

        // 2. Push telemetry on the configured GPS cadence.
        TelemetryMsg msg;
        msg.type = MSG_TELEMETRY;
        msg.speedKmph = gps.getSpeed(imuFeedback.totalGForce, imuFeedback.gyroZ);
        msg.gForceX = imuFeedback.gForceX; // Lateral Gs for cornering
        msg.gForceY = imuFeedback.gForceY; // Longitudinal Gs for braking/accel
        msg.totalGForce = imuFeedback.totalGForce; // Combined G-Force magnitude for the needle
        msg.gyroZ = imuFeedback.gyroZ;
        msg.lat = gps.getLat();
        msg.lng = gps.getLng();
        msg.sats = (uint8_t)gps.getSatellites();
        msg.hasFix = gps.hasFix() ? 1 : 0;
        msg.timestamp = gps.getEpochMs();
        msg.helmetBattery = (uint8_t)battery.getPercentage();
        msg.usedFreshImu = usedFreshImu;
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

unsigned long buttonPressTime = 0;
bool isButtonPressed = false;
bool ignoreInitialBootPress = true; // Prevents turning off immediately if you hold it while booting
void checkOffButton(){
    // Remember: Because of Q4, LOW means pressed, HIGH means released
    bool currentButtonState = (digitalRead(OFF_BUTTON_PIN) == LOW);
    // --- BUTTON STATE MACHINE ---
    if (currentButtonState && !isButtonPressed) {
        log_i("Off button pressed.");
        // The moment the button goes down
        buttonPressTime = millis();
        isButtonPressed = true;
    } else if (!currentButtonState && isButtonPressed) {
        log_i("Off button released.");
        // The moment the button is released
        isButtonPressed = false;
        ignoreInitialBootPress = false; // We successfully let go of the button after booting
    }

    // --- LONG PRESS DETECTION (SHUTDOWN) ---
    if (isButtonPressed && !ignoreInitialBootPress) {
        if (millis() - buttonPressTime >= HOLD_TIME_MS) {
            LOG_ERROR("Manual Shutdown Triggered! Release button to power off!");
            
            // Wait here until the user lets go of the button
            bool isLedOn = false;
            while(digitalRead(OFF_BUTTON_PIN) == LOW) {
                // Toggle the LED every 250ms to indicate shutdown mode
                digitalWrite(LED_BUILTIN, isLedOn ? LOW : HIGH);
                isLedOn = !isLedOn;
                delay(250); 
            }
            
            LOG_ERROR("Shutting down now.");
            unsigned long shutdownStart = millis();
            while(millis() - shutdownStart < CAPACITOR_DISCHARGE_TIME_MS) { // Wait up to 5 seconds for the capacitor to drain
                // Toggle the LED every 100ms to indicate shutdown mode
                digitalWrite(LED_BUILTIN, isLedOn ? LOW : HIGH);
                isLedOn = !isLedOn;
                delay(100);
            }

            // Release the latch to commit suicide
            digitalWrite(LATCH_PIN, LOW);

            // If we've made it here, we're must be connected to a usb cable for charging. Let's just go to deep sleep and wait for the power to cut instead of busy looping and draining the battery.
            esp_deep_sleep_start();
        }
    }
}


void setup() {
    pinMode(LATCH_PIN, OUTPUT);
    digitalWrite(LATCH_PIN, HIGH); // Ensure latch is HIGH on boot

    pinMode(OFF_BUTTON_PIN, INPUT_PULLUP);

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

#if ARDUINO_USB_CDC_ON_BOOT == 1
    // If the board is configured to use USB CDC on boot, we need to wait for the USB connection to be established before we can use Serial.
    delay(2000);
#endif

    Serial.begin(115200);

    unsigned long serialStart = millis();
    while (!Serial && millis() - serialStart < 2000) { delay(10); }

    log_i("--- KART LOGGER BOOTING ---");

    // 1. Filesystem & Audio
    if (!LittleFS.begin()) {
        LOG_ERROR("LittleFS Mount Failed!");
        while(1); // Halt if filesystem is dead
    }
    log_i("LittleFS Mounted. Waking up Audio...");
    
    // Initialize Error Logger
    if (!errorLogger.begin()) {
        LOG_ERROR("ErrorLogManager failed to initialize!");
    }
        
    // Start the audio engine on Core 0
    audio.begin(LittleFS, 10); 
    
    #if defined(HAS_STARTUP_AUDIO_CUES)
    audio.tryQueueAudio("/startup.wav");    
    audio.tryQueueAudio("/wait.wav");
    audio.tryQueueAudio("/initializing.wav");
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
        
        // If there's a stored error log, transmit it to the display
        if (errorLogger.logFileExists()) {
            log_i("Found stored error log. Transmitting to display...");
            #if defined(HAS_STARTUP_AUDIO_CUES)
            audio.tryQueueAudio("/initializing.wav");
            audio.tryQueueAudio("/wait.wav");
            #endif
            
            if (errorLogger.sendStoredLogsToDisplay()) {
                log_i("Error log transmitted successfully.");
                // Delete only after display confirms write via MSG_ERROR_LOG_ACK.
                errorLogger.deleteLogFile();
                
                #if defined(HAS_STARTUP_AUDIO_CUES)
                audio.tryQueueAudio("/ready.wav");
                audio.tryQueueAudio("/silence.wav");
                #endif
            } else {
                log_w("Failed to transmit error log to display.");
            }
        } else {
            log_d("No stored error log found. Continuing with normal boot.");
        }
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
        LOG_ERROR("Failed to start Fake GPS!");
        while(1);
    } else {
        log_i("Fake GPS Initialized with replay file.");
    }
    #else
    if (!gps.begin()) {
        LOG_ERROR("Failed to start GPS!");
        #if defined(HAS_STARTUP_AUDIO_CUES)
        audio.tryQueueAudio("/error.wav");
        audio.tryQueueAudio("/gps.wav");
        #endif
    }
    #endif

    // 7. Create Telemetry Queue (Holds up to 20 messages)
    telemetryQueue = xQueueCreate(20, sizeof(TelemetryMsg));
    if (telemetryQueue == NULL) {
        LOG_ERROR("Error creating the queue");
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

    // 8. Initialize the default finish line for lap timing (This can be updated later via a config or command)
    FinishLine defaultFinishLine = {
        -23.60488969289942, -46.836226585404766, // Left point (facing forward on the track)
        -23.604937937091048, -46.836415977188466 // Right point (facing forward on the track)
    };
    lapTimer.setFinishLine(defaultFinishLine);
}

void loop() {
    // Check if OFF button is being pressed to shut down the system (Hold for 3 seconds)
    checkOffButton();

    // --- IMU UPLINK PPS COUNTER ---
#if CORE_DEBUG_LEVEL == ARDUHAL_LOG_LEVEL_DEBUG
    static uint32_t lastSeenImuCounter = 0;
    static uint32_t lastPPSUpdateMs = 0;
    static uint32_t imuPpsThisSecond = 0;
    uint32_t now = millis();
    if (now - lastPPSUpdateMs >= 1000) {
        log_i("IMU Uplink: %d packets/sec", imuPpsThisSecond);
        imuPpsThisSecond = 0;
        lastPPSUpdateMs = now;
    }

    // Count new IMU packets received since last loop iteration
    ImuFeedbackMsg discardMsg = {};
    uint32_t currentImuCounter = 0;
    if (EspNowManager::getLatestImuFeedback(discardMsg, currentImuCounter)) {
        if (currentImuCounter > lastSeenImuCounter) {
            imuPpsThisSecond += (currentImuCounter - lastSeenImuCounter);
            lastSeenImuCounter = currentImuCounter;
        }
    }
#endif

#ifndef SMOKE_TEST
    TelemetryMsg msg;

    // Wait for a message in the queue (timeout after 100ms)
    if (xQueueReceive(telemetryQueue, &msg, pdMS_TO_TICKS(100)) == pdPASS) {
        
        // 1. ALWAYS Send to CYD (So the G-Force needle and Sat count work in the pits!)
        EspNowManager::sendTelemetry(msg);

        // --- THE GPS LOCK STATE MACHINE ---
        static bool isSystemReady = false;
        // static int lastSats = -1;
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
                if (millis() - lastAnnounceTime > 30000) { // msg.sats != lastSats && 
                    log_i("Searching... Satellites: %d", msg.sats);
                    
                    #if defined(HAS_STARTUP_AUDIO_CUES)
                    audio.tryQueueAudio("/wait.wav");
                    VoiceParser::queueNumber(audio, msg.sats);
                    audio.tryQueueAudio("/gps.wav");
                    audio.tryQueueAudio("/silence.wav");
                    #endif
                    
                    // lastSats = msg.sats;
                    lastAnnounceTime = millis();
                }
            }

            static uint32_t lastNoFixInfoMs = 0;
            if (millis() - lastNoFixInfoMs > 5000) {
                log_i("No GPS fix yet; telemetry/battery broadcast remains active.");
                lastNoFixInfoMs = millis();
            }
        } else {
            // ==========================================
            // STATE: SYSTEM READY (NORMAL RUNNING)
            // ==========================================

            // 2. Check for Lap Completion
            if (lapTimer.processTelemetry(msg)) {
                log_d("Lap Completed!");
                uint64_t lt = lapTimer.getLastLapTime();
                uint64_t bt = lapTimer.getBestLapTime();
                uint64_t pt = lapTimer.getPreviousLapTime();
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
        }

        // Debug Log
        log_d("SENT: Spd:%.1f G:%.2f Sats:%d Fix:%d | Batt: %.2f%%", 
              msg.speedKmph, msg.totalGForce, msg.sats, msg.hasFix, battery.getPercentage());
    }
#endif

    // Allow background tasks to breathe
    vTaskDelay(pdMS_TO_TICKS(10));
}