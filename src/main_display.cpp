#include <Arduino.h>

// Library includes from our new /lib folder
#include "EspNowManager.h"
#include "LogManager.h"
#include "EspNowProtocol.h"
#include "uiHelper.h"
#include "BatteryManager.h"
#include "ConfigManager.h"

#if defined(ENABLE_IMU)
#include "ImuManager.h"
#include "CalibrationManager.h"

#define I2C_SDA 18
#define I2C_SCL 17
#endif

#if defined(GPS_PROVIDER_ATGM336)
const uint8_t expectedPps = 10; // Expected packets per second from the logger (10Hz for ATGM336)
#else
const uint8_t expectedPps = 5; // Expected packets per second from the logger (5Hz for u-blox)
#endif

const uint8_t timeBetweenImuUplinkMessages = 10; // We expect a new IMU message at least every 10ms (100Hz), so this is our timeout for "freshness"

// --- GLOBAL STATE ---
LogManager logManager;
UiHelper uiHelper;

#if defined(ENABLE_IMU)
ImuManager imu;
CalibrationManager calibManager(imu);
ImuData latestImuData = {0};
bool imuReady = false;
uint32_t lastImuReadMs = 0;
#endif

// Display battery: BAT+ → 68k (683) → IO5 → 100k (01D) → GND (always-on divider)
// Theoretical ratio (68+100)/100 = 1.68, but empirical 4.224/2.47 = 1.71 absorbs the ~30mV ADC offset.
#define DISPLAY_BATT_ADC 5
#define DISPLAY_BATT_READ_INTERVAL_MS 1000
BatteryManager displayBattery(DISPLAY_BATT_ADC, 0xFF, 4.224f / 2.47f);
static uint32_t lastDisplayBattReadMs = 0;

// Smoothing & Metrics
float displaySpeed = 0;
float targetSpeed = 0;
const float lerpFactor = 0.20f;
uint32_t lastMessageCount = 0;
uint32_t pps = 0;
uint32_t lastPPSUpdate = 0;
uint8_t helmetBatteryCurrentLevel = 255;

// Error log transfer state (logger -> display -> SD)
static bool errorLogReceiving = false;
static uint16_t errorLogExpectedLines = 0;
static uint16_t errorLogWrittenLines = 0;
static bool errorLogWriteFailed = false;

static void processIncomingErrorLog() {
    uint16_t totalLines = 0;
    if (EspNowManager::consumeErrorLogStart(totalLines)) {
        errorLogReceiving = true;
        errorLogExpectedLines = totalLines;
        errorLogWrittenLines = 0;
        errorLogWriteFailed = false;

        if (!logManager.beginHelmetErrorLog()) {
            errorLogWriteFailed = true;
            log_w("Error log start failed in LogManager.");
        }

        log_i("Receiving helmet_error.log with %u lines.", totalLines);
    }

    ErrorLogLineMsg lineMsg = {};
    while (EspNowManager::popErrorLogLine(lineMsg)) {
        if (!errorLogReceiving || errorLogWriteFailed) {
            continue;
        }

        if (!logManager.appendHelmetErrorLine(lineMsg.lineData)) {
            errorLogWriteFailed = true;
            log_w("Error log line write failed in LogManager.");
            continue;
        }

        errorLogWrittenLines++;
    }

    if (EspNowManager::consumeErrorLogEnd(totalLines)) {
        uint16_t dropped = EspNowManager::getErrorLogDroppedLines();
        bool acked = errorLogReceiving &&
                     logManager.finalizeHelmetErrorLogAndAck(errorLogExpectedLines, errorLogWrittenLines, dropped, errorLogWriteFailed);

        if (!acked) {
            log_w("Error log incomplete. expected=%u written=%u dropped=%u", errorLogExpectedLines, errorLogWrittenLines, dropped);
        }

        errorLogReceiving = false;
    }
}

void syncUI() {
    bsp_display_lock(0);
    // 1. Get data from Radio
    if (EspNowManager::newDataAvailable) {
        EspNowManager::newDataAvailable = false;

        // Inject current steering angle before logging/processing
#if defined(ENABLE_IMU)
        if (imuReady) {
            EspNowManager::lastTelemetry.steeringAngle = calibManager.getSteeringAngle();

            uiHelper.setGx(EspNowManager::lastTelemetry.gForceX);
            uiHelper.setGy(EspNowManager::lastTelemetry.gForceY);
        }
#else
        uiHelper.setGx(EspNowManager::lastTelemetry.gForceX);
        uiHelper.setGy(EspNowManager::lastTelemetry.gForceY);
#endif

        // 2. Push to SD Log Queue (Non-blocking)
        if (LogManager::logQueue != NULL) {
            xQueueSend(LogManager::logQueue, &EspNowManager::lastTelemetry, 0);
        }

        // lastProcessedTimestamp = EspNowManager::lastTelemetry.timestamp;
        lastMessageCount++;

        uiHelper.setSpeed(EspNowManager::lastTelemetry.speedKmph);

        // 8. Update GPS Satellite Indicator
        uint8_t sats = EspNowManager::lastTelemetry.sats;
        static uint8_t lastSats = 255; // Use 255 so it guarantees an update on the very first loop
        if (sats != lastSats) {
            lastSats = sats;
            
            uiHelper.setGps(lastSats);
        }

        // 9. Update Helmet Battery Level
        static uint8_t lastBatteryLevel = 255; // Use 255 so it guarantees an update on the very first loop
        helmetBatteryCurrentLevel = EspNowManager::lastTelemetry.helmetBattery;
        if (helmetBatteryCurrentLevel >= 100) helmetBatteryCurrentLevel = 100; // Clamp to 100%

        if (helmetBatteryCurrentLevel != lastBatteryLevel) {
            lastBatteryLevel = helmetBatteryCurrentLevel;

            uiHelper.setHelmet(lastBatteryLevel);
        }
    }

    // --- SIGNAL HEALTH INDICATOR ---
    uiHelper.setPps(expectedPps, pps);

    bsp_display_unlock();
}

void setup() {
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT == 1
    delay(2000);
#endif
    Serial.setDebugOutput(true);

    log_i("--- KART DISPLAY BOOTING ---");

    // 1. Initialize IMU FIRST
#if defined(ENABLE_IMU)
    log_i("Initializing IMU on SDA:%d SCL:%d...", I2C_SDA, I2C_SCL);

    // We pass the pins directly to imu.begin to handle bus recovery and Wire.begin internally
    if (imu.begin(I2C_SDA, I2C_SCL, 100000)) {
        log_i("IMU: OK");
        imuReady = true;
    } else {
        log_e("IMU: Error - Check wiring on expansion port (GPIO 9/10)");
    }
#else
    log_i("Display IMU disabled (ENABLE_IMU not defined)");
#endif

    // 2. Initialize UI
    log_i("Display BSP init...");
    uiHelper.init();
    log_i("Display BSP done. Touch indev: %s", bsp_display_get_input_dev() ? "OK" : "NOT REGISTERED");

    // 3. Initialize Managers
    EspNowManager::begin();
    logManager.begin();
    displayBattery.begin();

    // 4. Load config from SD and apply saved theme
    configManager.begin();
    bsp_display_lock(0);
    uiHelper.setTheme(configManager.getTheme() == 0 ? DASH_MODE_NIGHT : DASH_MODE_DAY);
    bsp_display_unlock();

    // 4. Load config from SD and apply saved theme
    configManager.begin();
    bsp_display_lock(0);
    uiHelper.setTheme(configManager.getTheme() == 0 ? DASH_MODE_NIGHT : DASH_MODE_DAY);
    bsp_display_unlock();

#if defined(ENABLE_IMU)
    if (imuReady) {
        calibManager.begin();
    }
#endif

    log_i("Display System Ready.");
}

void loop() {
    uint32_t now = millis();

    processIncomingErrorLog();

#if defined(ENABLE_IMU)
    if (imuReady) {
        float currentSteering = calibManager.getSteeringAngle();

        if (calibManager.isDone() && (now - lastImuReadMs >= timeBetweenImuUplinkMessages)) {
            latestImuData = imu.update(currentSteering);
            lastImuReadMs = now;

            // Push IMU immediately so logger can consume a fresh sample
            // before building its next telemetry frame.
            ImuFeedbackMsg imuMsg = {};
            imuMsg.type = MSG_IMU_FEEDBACK;
            imuMsg.gForceX = latestImuData.accelX;
            imuMsg.gForceY = latestImuData.accelY;
            imuMsg.totalGForce = latestImuData.gForce;
            imuMsg.gyroZ = latestImuData.gyroZ;
            imuMsg.sampleMs = now;

            EspNowManager::sendImuFeedback(imuMsg);
        }
    }
#endif

    // Display battery ADC read — done before the display lock to avoid blocking LVGL
    static uint8_t cachedDisplayBattPct = 255;
    static uint8_t lastDisplayBattPct = 255;
    if (now - lastDisplayBattReadMs >= DISPLAY_BATT_READ_INTERVAL_MS) {
        lastDisplayBattReadMs = now;
        cachedDisplayBattPct = (uint8_t)displayBattery.getPercentage();
        log_i("Display battery: %d%%", cachedDisplayBattPct);
    }

    bsp_display_lock(0);

#if defined(ENABLE_IMU)
    if (imuReady) {
        calibManager.update();
    }
#endif

    // PPS Counter Logic
    if (now - lastPPSUpdate >= 1000) {
        uint32_t lastPps = lastMessageCount;
        lastMessageCount = 0;
        if (pps != lastPps) pps = lastPps;
        lastPPSUpdate = now;

#if defined(ENABLE_IMU)
        if (imuReady) {
            float steeringDeg = calibManager.getSteeringAngle();
            // Update latestImuData for the log magnitude even if not pushing to uplink yet
            latestImuData = imu.update(steeringDeg);
            log_i("Incoming Rate: %d pkts/sec | Steering: %.1f deg | G: %.2f\n", pps, steeringDeg, latestImuData.gForce);
        } else {
            log_i("Incoming Rate: %d pkts/sec\n", pps);
        }
#else
        log_i("Incoming Rate: %d pkts/sec\n", pps);
#endif
    }

    if (cachedDisplayBattPct != lastDisplayBattPct) {
        lastDisplayBattPct = cachedDisplayBattPct;
        uiHelper.setDisplay(cachedDisplayBattPct);
    }

    // Update UI Elements
    syncUI();

    bsp_display_unlock();
    delay(1);
}