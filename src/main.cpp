#include <Arduino.h>
#include <Wire.h>
#include "TelemetryData.h" // A nossa nova struct
#include "GpsManager.h"
#include "ImuManager.h"
#include "AudioManager.h"
#include "LapManager.h"
#include "Config.h"

#define MAX_TELNET_CLIENTS 3

// Instância real da memória compartilhada
volatile TelemetryData currentData;

GpsManager gps(44, 43);
ImuManager imu;
AudioManager audio(2, 3, 1);
FinishLine line = {FINISH_LINE.lat, FINISH_LINE.lng, 15.0}; 
LapManager lapTimer(gps, audio, line);

WiFiServer telnetServer(23);
WiFiClient clients[MAX_TELNET_CLIENTS];

void setupWiFi() {
    WiFi.begin("nGoline - Escritorio", "cuidadocomacabecadopimpolho");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n[WiFi] Conectado!");
    Serial.print("[WiFi] IP: ");
    Serial.println(WiFi.localIP());
    telnetServer.begin();
}

// Função para centralizar os logs (Serial + Telnet)
void logRemote(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // Envia para o USB
    Serial.print(buffer);

    // Se houver alguém conectado via Telnet, envia para o Wi-Fi
    for (int i = 0; i < MAX_TELNET_CLIENTS; i++) {
        if (clients[i] && clients[i].connected()) {
            clients[i].print(buffer);
        }
    }
}

void TaskGPS(void *pvParameters) {
    for (;;) {
        if (gps.update()) {
            currentData.lat = gps.getLat();
            currentData.lng = gps.getLng();
            currentData.speed = gps.getSpeed();
            currentData.sats = gps.getSatellites();
            currentData.hasFix = gps.hasFix();
            
            lapTimer.update();
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void TaskIMU(void *pvParameters) {
    uint32_t lastPrint = 0;
    for (;;) {
        // --- GERENCIADOR TELNET ---
        if (telnetServer.hasClient()) {
            bool added = false;
            for (int i = 0; i < MAX_TELNET_CLIENTS; i++) {
                if (!clients[i] || !clients[i].connected()) {
                    if (clients[i]) clients[i].stop();
                    clients[i] = telnetServer.available();
                    clients[i].println("=== TELEMETRIA KART REMOTA CONECTADA ===");
                    added = true;
                    break;
                }
            }
            if (!added) {
                // Já tem alguém, recusa o novo
                telnetServer.available().stop();
            }
        }

        ImuData imuReading = imu.update();
        currentData.gForce = imuReading.gForce;
        currentData.gyroZ = imuReading.gyroZ;
        
        // Log rápido para validação - Corrigido para %u ou %lu
        if (currentData.hasFix) {
            logRemote("G:%.2f | V:%.1f km/h | S:%u\n", 
                          currentData.gForce, 
                          currentData.speed, 
                          (unsigned int)currentData.sats); // Cast explícito resolve o warning
        }
        
        vTaskDelay(pdMS_TO_TICKS(20)); // 50Hz
    }
}

void setup() {
    Serial.begin(115200);
    Wire.begin(5, 6, 100000); 
    delay(2000);

    setupWiFi();

    gps.begin(); 
    if(imu.begin()) Serial.println("IMU: OK");
    if(audio.begin(21)) Serial.println("Audio: OK");

    xTaskCreatePinnedToCore(TaskGPS, "GPS_Task", 4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(TaskIMU, "IMU_Task", 4096, NULL, 2, NULL, 1);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}