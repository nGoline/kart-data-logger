#include <Arduino.h>
#include "GpsManager.h"
#include "BatteryManager.h"
#include "AudioManager.h"
#include "VoiceParser.h"
#include <WiFi.h>

// A string do Mario salva na Flash
const char testSound[] PROGMEM = "SuperMario:d=4,o=5,b=100:16e6,16e6,32p,8e6,16c6,8e6,8g6,8p,8g,8p,8c6,16p,8g,16p,8e,16p,8a,8b,16a#,8a,16g.,16e6,16g6,8a6,16f6,8g6,8e6,16c6,16d6,8b,16p";

// Configuração de Pinos
const int8_t GPS_RX = 7; 
const int8_t GPS_TX = 6;
const uint8_t BAT_PIN = 1;
const uint8_t I2S_BCLK = 2; // GPIO2 (D1)
const uint8_t I2S_LRC = 3;  // GPIO3 (D2)
const uint8_t I2S_DIN = 1;  // GPIO1 (D0)

// Instanciação dos Gerenciadores
GpsManager gps(GPS_RX, GPS_TX);
BatteryManager battery(BAT_PIN);
AudioManager audio(I2S_BCLK, I2S_LRC, I2S_DIN);

// Handles das Tasks
TaskHandle_t GpsTaskHandle = NULL;
TaskHandle_t LoggerTaskHandle = NULL;

void GpsTask(void *pvParameters) {
    for (;;) {
        gps.update();
        vTaskDelay(pdMS_TO_TICKS(1)); // Previne o travamento do Watchdog
    }
}

void LoggerTask(void *pvParameters) {
    for (;;) {
    //     // Grava dados apenas se tivermos um sinal limpo (mínimo de 4 satélites)
    //     if (gps.hasFix()) {
    //         storage.logData(millis(), gps.getLat(), gps.getLng(), gps.getSpeed());
    //     }
        
    //     // Atraso de 200ms bate exatamente com os 5Hz do GPS
        vTaskDelay(pdMS_TO_TICKS(200)); 
    }
}

void setup() {
    Serial.begin(115200);
    
    // Desliga rádio para evitar jitter no I2S
    WiFi.mode(WIFI_OFF); 
    
    delay(2000);

    if (LittleFS.begin(true)) {
        // --- SE TIVER CÓDIGO DE LISTAR ARQUIVOS AQUI, FECHE TUDO ---
        // File root = LittleFS.open("/"); ... root.close();
    }

    if (audio.begin(21)) {
        Serial.println("Áudio iniciado com sucesso.");
    }

    battery.begin();
    
    // if (!storage.begin()) {
    //     Serial.println("Erro: Falha ao montar o LittleFS!");
    // }
    
    gps.begin();

    // Core 0: Dedicado exclusivamente à leitura serial do GPS
    xTaskCreatePinnedToCore(GpsTask, "GpsTask", 4096, NULL, 2, &GpsTaskHandle, 0);
    
    // Core 1: Cuida da gravação no disco (que pode ter latência)
    xTaskCreatePinnedToCore(LoggerTask, "LoggerTask", 4096, NULL, 1, &LoggerTaskHandle, 1);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(100));
}