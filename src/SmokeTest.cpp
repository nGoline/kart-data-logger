#ifdef SMOKE_TEST

#include <Arduino.h>
#include "TelemetryData.h"

extern QueueHandle_t telemetryQueue;

void smokeTask(void* param) {
    // Simulate telemetry at 5Hz
    TelemetryData t = {};
    t.lat = -23.0;
    t.lng = -46.0;
    t.speed = 0.0;
    t.sats = 8;
    t.hasFix = true;
    t.gForce = 0.0f;

    for (;;) {
        t.lastUpdate = millis();
        t.speed += 0.5; if (t.speed > 120) t.speed = 0;
        t.gForce = 1.0f + 0.1f * sin((float)millis() / 1000.0f);
        if (telemetryQueue) xQueueSend(telemetryQueue, &t, 0);
        vTaskDelay(pdMS_TO_TICKS(200)); // 5Hz
    }
}

void startSmokeTest() {
    xTaskCreate(smokeTask, "SmokeSim", 2048, NULL, 1, NULL);
}

#endif
