#include <Arduino.h>
#include <Wire.h>
#include <string.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>
#include <WebServer.h>
#include "TelemetryData.h"
#include "GpsManager.h"
#include "ImuManager.h"
#include "AudioManager.h"
#include "LogManager.h"
#include "LapManager.h"
#include "Config.h"
#include "WifiManager.h"

// Telnet is handled inside WifiManager now

// Instância real da memória compartilhada
volatile TelemetryData currentData;

GpsManager gps(44, 43);
ImuManager imu;
AudioManager audio(2, 3, 1);
FinishLine line = {FINISH_LINE.lat, FINISH_LINE.lng, 15.0};
LapManager lapTimer(gps, audio, line);
LogManager logManager;

// Simple HTTP server to list and download logs
WebServer httpServer(80);

QueueHandle_t telemetryQueue = NULL;

// Global WifiManager so logRemote() can broadcast to connected telnet clients
WifiManager wifiMgr("nGoline - Escritorio", "cuidadocomacabecadopimpolho");

// GPS state is handled inside GpsManager; no globals required here.

// Função para centralizar os logs (Serial + Telnet)
void logRemote(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // Envia para o USB
    Serial.print(buffer);

    // Broadcast to any connected telnet clients via WifiManager
    wifiMgr.broadcast(buffer);
}

// GPS and IMU tasks are managed by their respective manager classes.

void setup() {
    Serial.begin(115200);
    delay(2000);
    Wire.begin(SDA, SCL, 100000);

    // Play initializing hint immediately (non-blocking)
    audio.tryQueueAudio("/wait.wav");
    audio.tryQueueAudio("/initializing.wav");
    audio.tryQueueAudio("/silence.wav");

    // Create telemetry queue before starting subsystems
    telemetryQueue = xQueueCreate(64, sizeof(TelemetryData));
    if (!telemetryQueue) {
        Serial.println("[Main] Failed to create telemetry queue");
    }

    // Start persistent logger on Core 1
    if (logManager.begin(telemetryQueue)) {
        Serial.println("[LogManager] Started");
        audio.tryQueueAudio("/file_system.wav");
        audio.tryQueueAudio("/ready.wav");
    } else {
        audio.tryQueueAudio("/error.wav");
        audio.tryQueueAudio("/file_system.wav");
    }
    audio.tryQueueAudio("/silence.wav");

    // Start audio hardware (announce on Serial only)
    if (audio.begin(21)) Serial.println("Audio: OK");

    // Use WifiManager to handle WiFi, OTA, HTTP and telnet
    wifiMgr.setTelemetryQueue(telemetryQueue);
    if (!wifiMgr.begin()) {
        Serial.println("[WiFi] Failed to connect");
    }

    // Start GPS (announce initialization)
    gps.begin();

    // Start GPS task via its manager
    gps.startTask(telemetryQueue, &lapTimer, &audio);

    // Start IMU and announce result
    audio.tryQueueAudio("/initializing.wav");
    audio.tryQueueAudio("/imu.wav");
    audio.tryQueueAudio("/silence.wav");
    if (imu.begin()) {
        Serial.println("IMU: OK");
        audio.tryQueueAudio("/imu.wav");
        audio.tryQueueAudio("/ready.wav");
    } else {
        audio.tryQueueAudio("/error.wav");
        audio.tryQueueAudio("/imu.wav");
    }
    audio.tryQueueAudio("/silence.wav");

    // Now start IMU periodic task
    imu.startTask();

    // optionally start smoke test
#ifdef SMOKE_TEST
    extern void startSmokeTest();
    startSmokeTest();
#endif

    audio.tryQueueAudio("/system_ready.wav");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}