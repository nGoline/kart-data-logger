#ifndef ESP_NOW_PROTOCOL_H
#define ESP_NOW_PROTOCOL_H

#include <Arduino.h>

// 1. Message Type IDs
enum MsgType : uint8_t {
    MSG_DISCOVER_REQ  = 0x01,
    MSG_DISCOVER_RESP = 0x02,
    MSG_TELEMETRY     = 0x10,
    MSG_IMU_FEEDBACK  = 0x11,
    MSG_LAP_COMPLETED = 0x20
};

// 2. The Core Data Struct (The "Contract" between boards)
// Using fixed-width types (float/uint32_t) ensures 
// both ESP32s see the exact same byte alignment.
struct __attribute__((packed)) TelemetryMsg {
    uint8_t type;         // MsgType
    float speedKmph;
    float gForceX; // Lateral Gs (Cornering)
    float gForceY; // Longitudinal Gs (Braking/Accel)
    float totalGForce; // Combined G-Force Magnitude (for G-Force needle)
    float gyroZ;
    double lat;
    double lng;
    uint8_t sats;
    uint8_t hasFix;
    uint64_t timestamp;
    uint8_t helmetBattery;
};

// Display -> Logger IMU uplink packet.
// The logger folds this sample into the next telemetry frame so logs can
// capture the exact IMU values used by speed filtering.
struct __attribute__((packed)) ImuFeedbackMsg {
    uint8_t type;         // MsgType
    float gForceX;        // Lateral Gs
    float gForceY;        // Longitudinal Gs
    float totalGForce;    // Combined magnitude
    float gyroZ;          // Yaw rate (deg/s)
    uint32_t sampleMs;    // Display uptime timestamp for freshness tracking
};

#endif