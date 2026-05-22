#include "LapManager.h"

void LapManager::setFinishLine(const FinishLine& line) {
    _gate = line;
    _hasLastPoint = false;
    currentLapStartTime = 0;
    lastLapTimeMs = 0;
    previousLapTimeMs = 0;
    bestLapTimeMs = 0xFFFFFFFFFFFFFFFFULL;
    log_i("LapManager: New Finish Line Set.");
}

// Quick Haversine distance for logging purposes
double getDistance(double lat1, double lon1, double lat2, double lon2) {
    double p = 0.017453292519943295; // Math.PI / 180
    double a = 0.5 - cos((lat2 - lat1) * p)/2 + 
               cos(lat1 * p) * cos(lat2 * p) * (1 - cos((lon2 - lon1) * p))/2;
    return 12742000 * asin(sqrt(a)); // 2 * R; R = 6371 km
}

bool LapManager::processTelemetry(const TelemetryMsg& data) {
    if (!data.hasFix) return false;

    // We need at least two points to draw a line segment
    if (!_hasLastPoint) {
        _lastLat = data.lat;
        _lastLng = data.lng;
        _lastTime = data.timestamp;
        _hasLastPoint = true;
        return false;
    }

    double centerLat = (_gate.leftLat + _gate.rightLat) / 2.0;
    double centerLng = (_gate.leftLng + _gate.rightLng) / 2.0;

    bool crossed = false;
    double fraction = 0.0;

    double distanceToGateCenter = getDistance(data.lat, data.lng, centerLat, centerLng);
    if (distanceToGateCenter < 10.0) {
        crossed = checkLineCrossing(
            _lastLng, _lastLat, data.lng, data.lat, 
            _gate.leftLng, _gate.leftLat, _gate.rightLng, _gate.rightLat, 
            fraction
        );
        log_d("Approaching Gate: %.1fm away", distanceToGateCenter);
    }

    // Save current point for the next loop iteration BEFORE we return
    uint64_t timeA = _lastTime;
    _lastLat = data.lat;
    _lastLng = data.lng;
    _lastTime = data.timestamp;

    // Cooldown: Prevent double-triggering if sitting on the start line
    if (crossed) {
        if (data.timestamp - currentLapStartTime > 10000) { 
            // INTERPOLATION: Calculate the exact millisecond the kart breached the line
            uint64_t crossingTimeMs = timeA + (uint64_t)(fraction * (data.timestamp - timeA));
            
            if (currentLapStartTime != 0) {
                previousLapTimeMs = lastLapTimeMs; // Move current to previous before updating
                lastLapTimeMs = crossingTimeMs - currentLapStartTime;
                
                // Check for Best Lap
                if (lastLapTimeMs < bestLapTimeMs) {
                    bestLapTimeMs = lastLapTimeMs;
                }
            }
            
            // Set the start time of the next lap to the exact interpolated crossing time
            currentLapStartTime = crossingTimeMs; 
            return true;
        } else {
            log_d("Lap crossed but in cooldown. Time since last lap: %llu ms", data.timestamp - currentLapStartTime);
        }
    }
    return false;
}

// 2D Line Segment Intersection Algorithm
bool LapManager::checkLineCrossing(double Ax, double Ay, double Bx, double By, 
                                   double Cx, double Cy, double Dx, double Dy, 
                                   double &fraction) {
    // Vector from A to B (Trajectory)
    double s1_x = Bx - Ax;
    double s1_y = By - Ay;
    
    // Vector from C to D (Finish Line Gate)
    double s2_x = Dx - Cx;
    double s2_y = Dy - Cy;

    double denom = s1_x * s2_y - s2_x * s1_y;
    if (denom == 0) return false; // Lines are parallel

    bool denomPositive = denom > 0;
    double s3_x = Ax - Cx;
    double s3_y = Ay - Cy;

    double s_num = s1_x * s3_y - s1_y * s3_x;
    
    // 1. Did the car cross the infinite line THIS exact frame? (0.0 to 1.0)
    if ((s_num < 0) == denomPositive || (s_num > denom) == denomPositive) {
        return false; // Hasn't reached the line, or already passed it
    }

    // 2. We crossed the line! But was it INSIDE the left/right gate posts?
    double t_num = s2_x * s3_y - s2_y * s3_x;
    if ((t_num < 0) == denomPositive || (t_num > denom) == denomPositive) {
        log_d("Gate Missed! You crossed the line, but OUTSIDE the Left/Right posts.");
        return false;
    }

    // 3. We are inside the gate! DIRECTION CHECK
    double dotProduct = s1_x * (Cy - Dy) + s1_y * (Dx - Cx);
    if (dotProduct <= 0) {
        log_d("Crossed Backwards! Left/Right points are swapped. (IGNORED FOR DESK TEST)");
        return false;
    }

    fraction = s_num / denom; // Exact millisecond percentage
    log_d("VALID CROSSING! Fraction: %.3f", fraction);
    return true;
}