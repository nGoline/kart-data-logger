#ifndef ESP_NOW_PROTOCOL_H
#define ESP_NOW_PROTOCOL_H

#include <Arduino.h>

// 1. Message Type IDs
enum MsgType : uint8_t {
    MSG_DISCOVER_REQ      = 0x01,
    MSG_DISCOVER_RESP     = 0x02,
    MSG_TELEMETRY         = 0x10,
    MSG_IMU_FEEDBACK      = 0x11,
    MSG_LAP_COMPLETED     = 0x20,
    MSG_ERROR_LOG_START   = 0x30,  // Start of error log transmission
    MSG_ERROR_LOG_LINE    = 0x31,  // Error log line (payload)
    MSG_ERROR_LOG_END     = 0x32,  // End of error log transmission
    MSG_ERROR_LOG_ACK     = 0x33   // Acknowledge receipt of error log
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
    bool usedFreshImu;
    float steeringAngle;  // Steering wheel angle in degrees
};

static_assert(sizeof(float) == 4, "Telemetry protocol requires 4-byte float");
static_assert(sizeof(uint64_t) == 8, "Telemetry protocol requires 8-byte uint64_t");

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

// Logger -> Display error log transmission messages
// Each line is sent separately to avoid payload size issues
struct __attribute__((packed)) ErrorLogLineMsg {
    uint8_t type;         // MSG_ERROR_LOG_LINE
    uint16_t lineNumber;  // Line sequence number (0-based)
    uint16_t totalLines;  // Total number of lines in the file
    char lineData[200];   // Line content (null-terminated)
};

// Logger -> Display: Start/End markers for error log transmission
struct __attribute__((packed)) ErrorLogControlMsg {
    uint8_t type;         // MSG_ERROR_LOG_START, MSG_ERROR_LOG_END, or MSG_ERROR_LOG_ACK
    uint16_t totalLines;  // Total lines in log (for START), or lines received (for ACK)
};

#endif