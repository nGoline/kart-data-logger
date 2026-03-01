#ifndef TELEMETRY_DATA_H
#define TELEMETRY_DATA_H

struct TelemetryData {
    double latitude;
    double longitude;
    double speedKmph;
    uint32_t satellites;
    uint32_t timestamp;
    bool isValid;
};

#endif