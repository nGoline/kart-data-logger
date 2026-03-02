#ifndef TELEMETRY_DATA_H
#define TELEMETRY_DATA_H

#include <Arduino.h>

struct TelemetryData {
    // Dados do GPS
    double lat;
    double lng;
    double speed;
    uint32_t sats;
    bool hasFix;
    
    // Dados do IMU
    float gForce;
    float gyroZ;
    
    // Metadados
    uint32_t lastUpdate;
};

// Declaramos como extern para que o compilador saiba que a 
// instância real será definida no main.cpp
extern volatile TelemetryData currentData;

#endif