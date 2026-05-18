#ifndef LAP_MANAGER_H
#define LAP_MANAGER_H

#include <Arduino.h>
#include "EspNowProtocol.h" // For TelemetryMsg/Data

// Define the track width by placing a point on the left and right sides
struct FinishLine {
    double leftLat;
    double leftLng;
    double rightLat;
    double rightLng;
};

class LapManager {
public:
    void setFinishLine(const FinishLine& line);

    // Returns true if a lap was just completed
    bool processTelemetry(const TelemetryMsg& data);

    uint64_t getLastLapTime() const { return lastLapTimeMs; }
    uint64_t getPreviousLapTime() const { return previousLapTimeMs; }
    uint64_t getBestLapTime() const { return bestLapTimeMs; }

private:
    FinishLine _gate;
    int32_t _deltaLast = 0;

    // Tracking the previous point to draw a line segment
    bool _hasLastPoint = false;
    double _lastLat;
    double _lastLng;
    uint64_t _lastTime;

    // Tracking the previous lap time
    uint64_t currentLapStartTime;
    uint64_t previousLapTimeMs;
    uint64_t lastLapTimeMs;
    uint64_t bestLapTimeMs;
    
    const uint32_t MIN_LAP_TIME_MS = 15000; // Increased to 15s for karts

    bool checkLineCrossing(double Ax, double Ay, double Bx, double By, 
                           double Cx, double Cy, double Dx, double Dy, 
                           double &fraction);
};

#endif