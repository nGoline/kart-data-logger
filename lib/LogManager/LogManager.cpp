#include "LogManager.h"

QueueHandle_t LogManager::logQueue = NULL;

LogManager::LogManager() {}

bool LogManager::begin(SemaphoreHandle_t spiMutex) {
    _spiMutex = spiMutex;
    
    // Ensure SD CS is High (Disabled) before we even start
    pinMode(TF_CS, OUTPUT); 
    digitalWrite(TF_CS, HIGH);

    logQueue = xQueueCreate(20, sizeof(TelemetryMsg));

    // Try to initialize SD card
    if (xSemaphoreTake(_spiMutex, pdMS_TO_TICKS(1000))) {
        // CYD SD is usually on VSPI (Pins 5, 18, 19, 23)
        if (!SD.begin(TF_CS)) {
            log_e("SD Card Mount Failed on Pin %d", TF_CS);
            _sdAvailable = false;
        } else {
            log_i("LogManager: SD Card Initialized.");
            _sdAvailable = true;
            createNewFile();
        }
        xSemaphoreGive(_spiMutex);
    }

    xTaskCreatePinnedToCore(LogManager::task, "LogTask", 4096, this, 1, NULL, 0);
    return _sdAvailable;
}

void LogManager::createNewFile() {
    // Basic incrementing file name: log_0.csv, log_1.csv...
    int i = 0;
    while (SD.exists("/log_" + String(i) + ".csv")) i++;
    _currentFileName = "/log_" + String(i) + ".csv";
    
    File f = SD.open(_currentFileName, FILE_WRITE);
    if (f) {
        f.println("epoch,speed,totalGForce,gForceX,gForceY,sats,lat,lng"); //CSV header
        f.close();
        log_i("LogManager: Started new log: %s", _currentFileName.c_str());
    }
}

void LogManager::task(void* param) {
    LogManager* self = (LogManager*)param;
    TelemetryMsg msg;

    for (;;) {
        if (xQueueReceive(logQueue, &msg, portMAX_DELAY)) {
            if (!self->_sdAvailable) continue;

            // Wait for SPI bus to be free from LVGL
            if (xSemaphoreTake(self->_spiMutex, pdMS_TO_TICKS(500))) {
                File f = SD.open(self->_currentFileName, FILE_APPEND);
                if (f) {
                    f.printf("%llu,%.1f,%.2f,%.2f,%.2f,%d,%.6f,%.6f\n", 
                             msg.timestamp, msg.speedKmph, msg.totalGForce, 
                             msg.gForceX, msg.gForceY, msg.sats, msg.lat, msg.lng);
                             f.close();
                }
                xSemaphoreGive(self->_spiMutex);
            } else {
                log_w("LogManager: SPI Timeout! Missed a log entry.");
            }
        }
    }
}