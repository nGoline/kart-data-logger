#ifndef CALIBRATION_MANAGER_H
#define CALIBRATION_MANAGER_H

#include <Arduino.h>
#include "lvgl.h"
#include "ImuManager.h"

enum class CalibrationState {
    IDLE,
    CENTER,
    LEFT,
    RIGHT,
    DONE
};

struct SteeringCalibration {
    float center;
    float maxLeft;
    float maxRight;
};

class CalibrationManager {
public:
    CalibrationManager(ImuManager& imu);
    void begin();
    void update();
    bool isDone() const { return _state == CalibrationState::DONE; }
    SteeringCalibration getCalibration() const { return _calib; }

    /**
     * @brief Get the current steering angle in degrees.
     * Maps the calibrated range to -90 (Full Left) to +90 (Full Right).
     */
    float getSteeringAngle() {
        ImuData data = _imu.update();
        float raw = data.accelY;

        if (_calib.maxLeft == _calib.maxRight) return 0; // Not calibrated

        // Map raw accelY to -90 to +90 degrees based on calibration
        // We use two linear segments (Center->Left and Center->Right) for better accuracy
        if (raw < _calib.center) {
            // Steering Left (assuming accelY decreases when turning left)
            // If accelY increases, the mapping will handle it.
            return mapFloat(raw, _calib.maxLeft, _calib.center, -90.0f, 0.0f);
        } else {
            // Steering Right
            return mapFloat(raw, _calib.center, _calib.maxRight, 0.0f, 90.0f);
        }
    }

private:
    float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
        if (in_min == in_max) return out_min;
        float val = (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
        // Clamp to range
        if (out_min < out_max) {
            if (val < out_min) val = out_min;
            if (val > out_max) val = out_max;
        } else {
            if (val > out_min) val = out_min;
            if (val < out_max) val = out_max;
        }
        return val;
    }
    void nextState();
    void updateUI();
    void createOverlay();
    void destroyOverlay();

    ImuManager& _imu;
    CalibrationState _state = CalibrationState::IDLE;
    uint32_t _stateStartMs = 0;
    const uint32_t _stepDurationMs = 5000;
    
    SteeringCalibration _calib = {0, 0, 0};

    // LVGL objects
    lv_obj_t* _overlay = nullptr;
    lv_obj_t* _titleLabel = nullptr;
    lv_obj_t* _infoLabel = nullptr;
    lv_obj_t* _timerLabel = nullptr;
    lv_obj_t* _bar = nullptr;
};

#endif // CALIBRATION_MANAGER_H
