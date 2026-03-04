#ifndef ESP_NOW_PROTOCOL_H
#define ESP_NOW_PROTOCOL_H

#include <Arduino.h>

// 1. Message Type IDs
enum MsgType : uint8_t {
    MSG_DISCOVER_REQ  = 0x01,
    MSG_DISCOVER_RESP = 0x02,
    MSG_TELEMETRY     = 0x10,
    MSG_LAP_COMPLETED = 0x20
};

// 2. The Core Data Struct (The "Contract" between boards)
// Using fixed-width types (float/uint32_t) ensures 
// both ESP32s see the exact same byte alignment.
struct TelemetryMsg {
    uint8_t type;         // MsgType
    float speedKmph;
    float gForce;
    float gyroZ;
    double lat;
    double lng;
    uint8_t sats;
    uint8_t hasFix;
    uint64_t timestamp;   // millis() from the Logger
};

#endif