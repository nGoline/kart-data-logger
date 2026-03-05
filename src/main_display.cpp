#include <Arduino.h>
#include <esp32_smartdisplay.h>
#include <esp_lcd_panel_ops.h>

// Library includes from our new /lib folder
#include "EspNowManager.h"
#include "LogManager.h"
#include "EspNowProtocol.h"
#include "ui.h"

// --- SPI BUS SHARING LOGIC ---
SemaphoreHandle_t spiMutex;

void my_threaded_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
        esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)disp->user_data;
        esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
        xSemaphoreGive(spiMutex);
    }
}

// --- GLOBAL STATE ---
LogManager logManager;
lv_obj_t* barras_speed[10];
static uint32_t lv_last_tick = 0;

// Smoothing & Metrics
float displaySpeed = 0;
float targetSpeed = 0;
const float lerpFactor = 0.20f;
uint32_t lastMessageCount = 0;
uint32_t pps = 0;
uint32_t lastPPSUpdate = 0;

void syncUI() {
    // 1. Get data from Radio
    if (EspNowManager::newDataAvailable) {
        EspNowManager::newDataAvailable = false;

        static uint64_t lastProcessedTimestamp = 0;
        if (EspNowManager::lastTelemetry.timestamp == lastProcessedTimestamp) {
            return; // This is a Wi-Fi echo/duplicate. Ignore it!
        }
        lastProcessedTimestamp = EspNowManager::lastTelemetry.timestamp;

        targetSpeed = EspNowManager::lastTelemetry.speedKmph;
        lastMessageCount++;

        // 2. Push to SD Log Queue (Non-blocking)
        if (LogManager::logQueue != NULL) {
            xQueueSend(LogManager::logQueue, &EspNowManager::lastTelemetry, 0);
        }
    }

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
    int num_barras_vivas = map((int)displaySpeed, 0, 100, 0, 10);
    for (int i = 0; i < 10; i++) {
        if (i < num_barras_vivas) lv_obj_clear_flag(barras_speed[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(barras_speed[i], LV_OBJ_FLAG_HIDDEN);
    }

    // 6. Update Digital Label
    char buf[12];
    snprintf(buf, sizeof(buf), "%.1f", displaySpeed);
    lv_label_set_text(ui_Label_speed, buf);

    // ==========================================
    // 7. Update GPS Satellite Indicator
    // ==========================================
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
}

void setup() {
    Serial.begin(115200);
    spiMutex = xSemaphoreCreateMutex();

    // 1. Initialize Display Hardware
    smartdisplay_init();
    auto disp = lv_display_get_default();
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);

    // 2. Hijack the Flush Callback for Thread Safety and Bit Endianess Fix
    lv_display_set_flush_cb(disp, my_threaded_flush);

    // 3. Initialize UI Widgets
    ui_init();
    barras_speed[0] = ui_speed1; barras_speed[1] = ui_speed2;
    barras_speed[2] = ui_speed3; barras_speed[3] = ui_speed4;
    barras_speed[4] = ui_speed5; barras_speed[5] = ui_speed6;
    barras_speed[6] = ui_speed7; barras_speed[7] = ui_speed8;
    barras_speed[8] = ui_speed9; barras_speed[9] = ui_speed10;

    // 4. Initialize Managers
    EspNowManager::begin();  // Starts scanning for logger
    logManager.begin(spiMutex); // Starts SD logging task
    
    lv_last_tick = millis();
    log_i("Display System Ready.");
}

void loop() {
    uint32_t now = millis();
    lv_tick_inc(now - lv_last_tick);
    lv_last_tick = now;

    // Background Radio Logic (Channel Hopping)
    EspNowManager::updateScanner();

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
            if (pps >= 5) {
                // Perfect 5Hz signal
                lv_obj_clear_flag(ui_centerGreen, LV_OBJ_FLAG_HIDDEN);
            } else if (pps >= 2) {
                // Dropping packets, but still connected
                lv_obj_clear_flag(ui_centerYellow, LV_OBJ_FLAG_HIDDEN);
            } else {
                // Dead link or completely disconnected
                lv_obj_clear_flag(ui_centerRed, LV_OBJ_FLAG_HIDDEN);
            }
        }

        lastPPSUpdate = now;
        Serial.printf("Incoming Rate: %d pkts/sec\n", pps);
    }

    // Update UI Elements
    syncUI();

    // Render Frame
    lv_timer_handler();
    delay(1);
}