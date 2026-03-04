#ifdef SMOKE_TEST

#include <Arduino.h>
#include "EspNowProtocol.h"
#include "EspNowManager.h"

extern QueueHandle_t telemetryQueue;

void smokeTask(void* param) {
    // Simulate telemetry at 5Hz
    TelemetryMsg t = {};
    t.type = MSG_TELEMETRY;
    t.lat = -23.0;
    t.lng = -46.0;
    t.speedKmph = 0.0;
    t.sats = 8;
    t.hasFix = true;
    t.gForce = 0.0f;
    // Start at 2026-03-02T18:00:00.000 UTC (milliseconds since epoch)
    uint64_t startMs = 1772474400000ULL;
    t.timestamp = startMs;

    log_i("SMOKE: Simulation starting...");

    for (;;) {
        // advance by 200ms per sample (5Hz)
        t.timestamp += 200ULL;
        t.speedKmph += 0.5; if (t.speedKmph > 100) t.speedKmph = 0;
        t.gForce = (t.speedKmph / 120.0f) * 2.5f;
        t.gyroZ = 15.0f * sin(millis() / 2000.0f);

        EspNowManager::sendTelemetry(t);

        // if (lapTimer.processTelemetry(t)) {
        //     uint32_t lt = lapTimer.getLastLapTime();
        //     VoiceParser::announceLapTime(audio, lt/60000, (lt%60000)/1000, lt%1000);
        // }

        vTaskDelay(pdMS_TO_TICKS(200)); // 5Hz
    }
}

void startSmokeTest() {
    xTaskCreate(smokeTask, "SmokeSim", 2048, NULL, 1, NULL);
}

#endif
