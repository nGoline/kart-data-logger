#include <Arduino.h>
#include "display.h"
#include "esp_bsp.h"
#include "lv_port.h"

// Library includes from our new /lib folder
#include "EspNowManager.h"
#include "LogManager.h"
#include "EspNowProtocol.h"
#include "ui.h"

#if defined(ENABLE_IMU)
#include "ImuManager.h"
#include "CalibrationManager.h"

#if defined(JC3248W535)
#define I2C_SDA 18
#define I2C_SCL 17
#else
#define I2C_SDA SDA
#define I2C_SCL SCL
#endif
#endif

#if defined(GPS_PROVIDER_ATGM336)
const uint8_t expectedPps = 10; // Expected packets per second from the logger (10Hz for ATGM336)
#else
const uint8_t expectedPps = 5; // Expected packets per second from the logger (5Hz for u-blox)
#endif

const uint8_t timeBetweenImuUplinkMessages = 10; // We expect a new IMU message at least every 10ms (100Hz), so this is our timeout for "freshness"

// --- GLOBAL STATE ---
LogManager logManager;
lv_obj_t* spped_bars[10];
lv_obj_t* gForce_bars[20];

#if defined(ENABLE_IMU)
ImuManager imu;
CalibrationManager calibManager(imu);
ImuData latestImuData = {0};
bool imuReady = false;
uint32_t lastImuReadMs = 0;
#endif

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
    // 1. Get data from Radio
    if (EspNowManager::newDataAvailable) {
        EspNowManager::newDataAvailable = false;

        // static uint64_t lastProcessedTimestamp = 0;
        // if (EspNowManager::lastTelemetry.timestamp == lastProcessedTimestamp) {
        //     return; // This is a Wi-Fi echo/duplicate. Ignore it!
        // }

        // Inject current steering angle before logging/processing
#if defined(ENABLE_IMU)
        if (imuReady) {
            EspNowManager::lastTelemetry.steeringAngle = calibManager.getSteeringAngle();
        }
#endif

        // 2. Push to SD Log Queue (Non-blocking)
        if (LogManager::logQueue != NULL) {
            xQueueSend(LogManager::logQueue, &EspNowManager::lastTelemetry, 0);
        }

        // lastProcessedTimestamp = EspNowManager::lastTelemetry.timestamp;
        targetSpeed = EspNowManager::lastTelemetry.speedKmph;
        lastMessageCount++;

        // 3. Smooth the needle movement (Lerp)
        if (abs(targetSpeed - displaySpeed) > 0.05) {
            displaySpeed += (targetSpeed - displaySpeed) * lerpFactor;
        } else {
            displaySpeed = targetSpeed;
        }
            
        // 4. Update Needle (0.1 degree units)
        float angle_decimal = (displaySpeed - 0) * (313 - (-723)) / (100 - 0) + (-723);
        lv_img_set_angle(ui_Image_needle, (int32_t)angle_decimal);

        // 5. Update Speed Bars
        int num_live_bars = map((int)displaySpeed, 0, 100, 0, 10);
        for (int i = 0; i < 10; i++) {
            if (i < num_live_bars) lv_obj_clear_flag(spped_bars[i], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(spped_bars[i], LV_OBJ_FLAG_HIDDEN);
        }

        // 6. Update Digital Label
        char buf[12];
        snprintf(buf, sizeof(buf), "%.1f", displaySpeed);
        lv_label_set_text(ui_Label_speed, buf);

        // 7. Update GForce Indicator
        float totalGForce = EspNowManager::lastTelemetry.totalGForce;
    #if defined(ENABLE_IMU)
        if (imuReady) {
            totalGForce = latestImuData.gForce;
        }
    #endif

        int activeGIndex = (int)((totalGForce - 0.125f) / 0.125f);
        if (activeGIndex < 0) activeGIndex = 0;
        else if (activeGIndex > 19) activeGIndex = 19;

        for (int i = 0; i < 20; i++) {
            if (i < activeGIndex) lv_obj_clear_flag(gForce_bars[i], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(gForce_bars[i], LV_OBJ_FLAG_HIDDEN);
        }

        // 8. Update GPS Satellite Indicator
        uint8_t sats = EspNowManager::lastTelemetry.sats;
        static uint8_t lastSats = 255; // Use 255 so it guarantees an update on the very first loop
        if (sats != lastSats) {
            lastSats = sats;
            // First, hide all colors to clear the previous state
            lv_obj_add_flag(ui_centerLeftRed, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_centerLeftYellow, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_centerLeftGreen, LV_OBJ_FLAG_HIDDEN);

            if (sats == 0) {
                // No satellites at all
                lv_label_set_text(ui_LabelGpsCount, "--");
            } else {
                // Update the text label
                char satBuf[8];
                snprintf(satBuf, sizeof(satBuf), "%d", sats);
                lv_label_set_text(ui_LabelGpsCount, satBuf);

                // Turn on the appropriate color bar
                if (sats >= 7) {
                    lv_obj_clear_flag(ui_centerLeftGreen, LV_OBJ_FLAG_HIDDEN);
                } else if (sats >= 4) {
                    lv_obj_clear_flag(ui_centerLeftYellow, LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_clear_flag(ui_centerLeftRed, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }

        // 9. Update Helmet Battery Level
        static uint8_t lastBatteryLevel = 255; // Initialize with impossible value
        helmetBatteryCurrentLevel = EspNowManager::lastTelemetry.helmetBattery;
        if (helmetBatteryCurrentLevel >= 100) helmetBatteryCurrentLevel = 99; // Clamp to 99%

        if (helmetBatteryCurrentLevel != lastBatteryLevel) {
            lastBatteryLevel = helmetBatteryCurrentLevel;

            // 1. Update the numeric label
            lv_label_set_text_fmt(ui_LabelBattery, "%d", helmetBatteryCurrentLevel);

            // 2. Handle the "Traffic Light" Visibility
            // Hide everything first, then reveal the correct one
            lv_obj_add_flag(ui_centerRightRed, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_centerRightYellow, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_centerRightGreen, LV_OBJ_FLAG_HIDDEN);

            if (helmetBatteryCurrentLevel > 60) {
                lv_obj_clear_flag(ui_centerRightGreen, LV_OBJ_FLAG_HIDDEN);
            } 
            else if (helmetBatteryCurrentLevel > 20) {
                lv_obj_clear_flag(ui_centerRightYellow, LV_OBJ_FLAG_HIDDEN);
            } 
            else {
                lv_obj_clear_flag(ui_centerRightRed, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    // 2. Dynamic "Danger Zone" Blinking (Runs every loop if level <= 20)
    if (helmetBatteryCurrentLevel <= 20) {
        // Blink every 500ms using the ESP32 internal clock
        bool show = (millis() / 500) % 2; 
        
        if (show) {
            lv_obj_clear_flag(ui_centerRightRed, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_opa(ui_LabelBattery, LV_OPA_COVER, 0);
        } else {
            lv_obj_add_flag(ui_centerRightRed, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_opa(ui_LabelBattery, LV_OPA_0, 0); // Blink the text too
        }
    } else {
        // Ensure text is visible if we are above the danger zone
        lv_obj_set_style_text_opa(ui_LabelBattery, LV_OPA_COVER, 0);
    }
}

void setup() {
#if ARDUINO_USB_CDC_ON_BOOT == 1
    // If the board is configured to use USB CDC on boot, we need to wait for the USB connection to be established before we can use Serial.
    delay(2000);
#endif

    Serial.begin(115200);
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

    // 2. Initialize Display Hardware
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = {
            .task_priority     = 4,
            .task_stack        = 16384,
            .task_affinity     = -1,
            .task_max_sleep_ms = 500,
            .timer_period_ms   = 5,
        },
        .buffer_size = EXAMPLE_LCD_QSPI_H_RES * EXAMPLE_LCD_QSPI_V_RES,
        .rotate = LV_DISPLAY_ROTATION_270,
    };
    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    bsp_display_lock(0);

    // 3. Initialize UI
    ui_init();
    spped_bars[0] = ui_speed1; spped_bars[1] = ui_speed2;
    spped_bars[2] = ui_speed3; spped_bars[3] = ui_speed4;
    spped_bars[4] = ui_speed5; spped_bars[5] = ui_speed6;
    spped_bars[6] = ui_speed7; spped_bars[7] = ui_speed8;
    spped_bars[8] = ui_speed9; spped_bars[9] = ui_speed10;
    gForce_bars[0] = ui_gForce1; gForce_bars[1] = ui_gForce2;
    gForce_bars[2] = ui_gForce3; gForce_bars[3] = ui_gForce4;
    gForce_bars[4] = ui_gForce5; gForce_bars[5] = ui_gForce6;
    gForce_bars[6] = ui_gForce7; gForce_bars[7] = ui_gForce8;
    gForce_bars[8] = ui_gForce9; gForce_bars[9] = ui_gForce10;
    gForce_bars[10] = ui_gForce11; gForce_bars[11] = ui_gForce12;
    gForce_bars[12] = ui_gForce13; gForce_bars[13] = ui_gForce14;
    gForce_bars[14] = ui_gForce15; gForce_bars[15] = ui_gForce16;
    gForce_bars[16] = ui_gForce17; gForce_bars[17] = ui_gForce18;
    gForce_bars[18] = ui_gForce19; gForce_bars[19] = ui_gForce20;

    bsp_display_unlock();

    // 4. Initialize Managers
    EspNowManager::begin();
    logManager.begin();

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
        if (pps != lastPps) {
            pps = lastPps;

            // --- SIGNAL HEALTH INDICATOR ---
            // 1. Hide all indicators first to clear the previous state
            lv_obj_add_flag(ui_centerRed, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_centerYellow, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_centerGreen, LV_OBJ_FLAG_HIDDEN);

            // 2. Unhide the correct one based on the telemetry rate
            if (pps >= expectedPps) {
                // Perfect 5Hz signal
                lv_obj_clear_flag(ui_centerGreen, LV_OBJ_FLAG_HIDDEN);
            } else if (pps >= expectedPps / 2) {
                // Dropping packets, but still connected
                lv_obj_clear_flag(ui_centerYellow, LV_OBJ_FLAG_HIDDEN);
            } else {
                // Dead link or completely disconnected
                lv_obj_clear_flag(ui_centerRed, LV_OBJ_FLAG_HIDDEN);
            }
        }

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

    // Update UI Elements
    syncUI();

    bsp_display_unlock();
    delay(1);
}