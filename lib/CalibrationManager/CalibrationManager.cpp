#include "CalibrationManager.h"

#if defined(ENABLE_IMU)

CalibrationManager::CalibrationManager(ImuManager& imu) : _imu(imu) {}

void CalibrationManager::begin() {
    _state = CalibrationState::CENTER;
    _stateStartMs = millis();
    createOverlay();
    updateUI();
    log_i("Calibration started: CENTER");
}

void CalibrationManager::update() {
    if (_state == CalibrationState::IDLE || _state == CalibrationState::DONE) {
        return;
    }

    uint32_t now = millis();
    uint32_t elapsed = now - _stateStartMs;

    if (elapsed >= _stepDurationMs) {
        nextState();
    } else {
        updateUI();
    }
}

void CalibrationManager::nextState() {
    // Sample current IMU data for the state we are LEAVING
    ImuData data = _imu.update();
    
    switch (_state) {
        case CalibrationState::CENTER:
            _imu.calibrateOffsets(); // Calibrate IMU while centered
            _calib.center = data.accelY; // Example axis
            log_i("Center calibrated. Value: %.2f", _calib.center);
            _state = CalibrationState::LEFT;
            break;
        case CalibrationState::LEFT:
            _calib.maxLeft = data.accelY;
            log_i("Max Left recorded. Value: %.2f", _calib.maxLeft);
            _state = CalibrationState::RIGHT;
            break;
        case CalibrationState::RIGHT:
            _calib.maxRight = data.accelY;
            log_i("Max Right recorded. Value: %.2f", _calib.maxRight);
            _state = CalibrationState::DONE;
            destroyOverlay();
            log_i("Calibration DONE.");
            return; // Exit early
        default:
            break;
    }

    _stateStartMs = millis();
    updateUI();
}

void CalibrationManager::createOverlay() {
    // Use the active screen
    lv_obj_t* scr = lv_scr_act();
    
    _overlay = lv_obj_create(scr);
    lv_obj_set_size(_overlay, 280, 200);
    lv_obj_set_align(_overlay, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_color(_overlay, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(_overlay, 2, 0);
    lv_obj_set_style_border_color(_overlay, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(_overlay, 10, 0);
    lv_obj_set_style_bg_opa(_overlay, 230, 0);
    
    _titleLabel = lv_label_create(_overlay);
    lv_obj_set_align(_titleLabel, LV_ALIGN_TOP_MID);
    lv_obj_set_y(_titleLabel, 10);
    lv_obj_set_style_text_font(_titleLabel, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(_titleLabel, lv_color_hex(0xFFFFFF), 0);
    
    _infoLabel = lv_label_create(_overlay);
    lv_obj_set_width(_infoLabel, 260);
    lv_obj_set_align(_infoLabel, LV_ALIGN_CENTER);
    lv_obj_set_y(_infoLabel, -10);
    lv_obj_set_style_text_align(_infoLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(_infoLabel, lv_color_hex(0xEEEEEE), 0);
    
    _timerLabel = lv_label_create(_overlay);
    lv_obj_set_align(_timerLabel, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(_timerLabel, -40);
    lv_obj_set_style_text_font(_timerLabel, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(_timerLabel, lv_color_hex(0x00FF00), 0);
    
    _bar = lv_bar_create(_overlay);
    lv_obj_set_size(_bar, 200, 10);
    lv_obj_set_align(_bar, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(_bar, -15);
    lv_bar_set_range(_bar, 0, 100);
}

void CalibrationManager::updateUI() {
    if (!_overlay) return;

    uint32_t now = millis();
    uint32_t elapsed = now - _stateStartMs;
    int remainingSec = (_stepDurationMs - elapsed) / 1000 + 1;
    if (remainingSec < 1) remainingSec = 1;

    int progress = (elapsed * 100) / _stepDurationMs;
    lv_bar_set_value(_bar, progress, LV_ANIM_OFF);

    char buf[16];
    snprintf(buf, sizeof(buf), "%ds", remainingSec);
    lv_label_set_text(_timerLabel, buf);

    switch (_state) {
        case CalibrationState::CENTER:
            lv_label_set_text(_titleLabel, "STEERING CALIBRATION");
            lv_label_set_text(_infoLabel, "KEEP WHEEL CENTERED\nAND STEADY");
            break;
        case CalibrationState::LEFT:
            lv_label_set_text(_titleLabel, "STEERING CALIBRATION");
            lv_label_set_text(_infoLabel, "TURN FULL LEFT\nAND HOLD");
            break;
        case CalibrationState::RIGHT:
            lv_label_set_text(_titleLabel, "STEERING CALIBRATION");
            lv_label_set_text(_infoLabel, "TURN FULL RIGHT\nAND HOLD");
            break;
        default:
            break;
    }
}

void CalibrationManager::destroyOverlay() {
    if (_overlay) {
        lv_obj_del(_overlay);
        _overlay = nullptr;
        _titleLabel = nullptr;
        _infoLabel = nullptr;
        _timerLabel = nullptr;
        _bar = nullptr;
    }
}

#endif // ENABLE_IMU