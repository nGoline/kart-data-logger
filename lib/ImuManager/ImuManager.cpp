#include "ImuManager.h"

#if defined(ENABLE_IMU)

ImuManager::ImuManager() : _mpu(0x68) {}

void ImuManager::recoverStuckBus() {
    if (_sda_pin < 0 || _scl_pin < 0) {
        return;
    }

    pinMode(_scl_pin, OUTPUT);
    for (int i = 0; i < 9; i++) {
        digitalWrite(_scl_pin, LOW);
        delayMicroseconds(5);
        digitalWrite(_scl_pin, HIGH);
        delayMicroseconds(5);
    }

    pinMode(_sda_pin, INPUT_PULLUP);
    pinMode(_scl_pin, INPUT_PULLUP);
}

bool ImuManager::restartI2cBus() {
    Wire.end();
    delay(2);

    if (_sda_pin >= 0 && _scl_pin >= 0) {
        return Wire.begin(_sda_pin, _scl_pin, _i2cFrequency);
    }

    return Wire.begin();
}

bool ImuManager::begin(int sda, int scl, uint32_t i2cFrequency, bool recoverBusOnBegin) {
    _sda_pin = sda;
    _scl_pin = scl;
    _i2cFrequency = i2cFrequency;
    _recoverBusOnBegin = recoverBusOnBegin;

    if (_recoverBusOnBegin) {
        recoverStuckBus();
        delay(20);
    }

    if (!restartI2cBus()) {
        log_e("IMU: Failed to initialize I2C bus.");
        return false;
    }

    delay(5);

    _mpu.initialize();

    if (!_mpu.testConnection()) {
        log_e("IMU: MPU6050 connection failed!");
        return false;
    }

    // Kart-specific tuning
    _mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_8); 
    _mpu.setFullScaleGyroRange(MPU6050_GYRO_FS_500); 
    _mpu.setDLPFMode(MPU6050_DLPF_BW_20);            

    log_i("IMU: MPU6050 Initialized.");
    return true;
}

void ImuManager::calibrateOffsets() {
    log_i("IMU: Capturing center gravity vector (STAY STILL)...");
    
    // Explicitly reset hardware offsets to ensure we are reading raw values
    _mpu.setXAccelOffset(0);
    _mpu.setYAccelOffset(0);
    _mpu.setZAccelOffset(0);
    _mpu.setXGyroOffset(0);
    _mpu.setYGyroOffset(0);
    _mpu.setZGyroOffset(0);
    delay(200); 

    // Average 100 samples for better precision
    long sax = 0, say = 0, saz = 0;
    long sgx = 0, sgy = 0, sgz = 0;
    const int samples = 100;
    
    for (int i = 0; i < samples; i++) {
        int16_t ax, ay, az, gx, gy, gz;
        _mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
        sax += ax; say += ay; saz += az;
        sgx += gx; sgy += gy; sgz += gz;
        delay(2);
    }

    // We use software-only calibration because hardware CalibrateAccel()
    // only works if the sensor is perfectly level (Z up).
    _gravityX = (float)sax / samples / 4096.0f;
    _gravityY = (float)say / samples / 4096.0f;
    _gravityZ = (float)saz / samples / 4096.0f;
    
    _gyroBiasX = (float)sgx / samples / 65.5f;
    _gyroBiasY = (float)sgy / samples / 65.5f;
    _gyroBiasZ = (float)sgz / samples / 65.5f;
    
    log_i("IMU: Capture complete. Gravity: X=%.2f, Y=%.2f, Z=%.2f", _gravityX, _gravityY, _gravityZ);
}

ImuData ImuManager::update(float steeringAngleDeg) {
    ImuData data = {0};
    int16_t ax, ay, az, gx, gy, gz;

    if (!_mpu.testConnection()) {
        log_e("IMU: I2C Bus crashed! Initiating Auto-Recovery...");
        if (!restartI2cBus()) return data;
        delay(5);
        _mpu.initialize();
        if (!_mpu.testConnection()) return data;
    }

    _mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

    // Raw readings in G and Deg/s
    float axG = ax / 4096.0f;
    float ayG = ay / 4096.0f;
    float azG = az / 4096.0f;

    // Compensate for gravity shift when the wheel is turned.
    // The rotation is around the Z axis.
    float phi = -steeringAngleDeg * 0.0174532925f; // to radians
    float cosPhi = cos(phi);
    float sinPhi = sin(phi);

    // Calculate how the center gravity vector rotates with the steering wheel
    float expGX = _gravityX * cosPhi - _gravityY * sinPhi;
    float expGY = _gravityX * sinPhi + _gravityY * cosPhi;
    float expGZ = _gravityZ;

    // Subtract rotating gravity to leave only linear acceleration
    data.accelX = axG - expGX;
    data.accelY = ayG - expGY;
    data.accelZ = azG - expGZ;

    // Subtract gyro bias
    data.gyroX = (gx / 65.5f) - _gyroBiasX;
    data.gyroY = (gy / 65.5f) - _gyroBiasY;
    data.gyroZ = (gz / 65.5f) - _gyroBiasZ;

    // Lateral + Longitudinal magnitude (ignoring vertical bumps for UI)
    data.gForce = sqrt(pow(data.accelX, 2) + pow(data.accelY, 2));

    return data;
}

#endif // ENABLE_IMU