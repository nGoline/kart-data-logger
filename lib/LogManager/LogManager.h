#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "EspNowProtocol.h"

class LogManager {
public:
    LogManager();
    
    // Pass the shared SPI Mutex here
    bool begin(SemaphoreHandle_t spiMutex);
    
    // The background task that writes to SD
    static void task(void* param);

    // Queue to receive data from the EspNowManager
    static QueueHandle_t logQueue;

private:
    SemaphoreHandle_t _spiMutex;
    bool _sdAvailable = false;
    String _currentFileName;
    
    void createNewFile();
};

#endif