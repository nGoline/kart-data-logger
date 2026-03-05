#include "ImuManager.h"

ImuManager::ImuManager() : _mpu(0x68) {}

bool ImuManager::begin(int sda, int scl) {
    _sda_pin = sda;
    _scl_pin = scl;

    _mpu.initialize();

    if (!_mpu.testConnection()) {
        log_e("IMU: MPU6050 connection failed!");
        return false;
    }

    // Kart-specific tuning
    _mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_8); 
    _mpu.setFullScaleGyroRange(MPU6050_GYRO_FS_500); 
    _mpu.setDLPFMode(MPU6050_DLPF_BW_20);            

    log_i("IMU: MPU6050 Initialized. Calibrating...");
    
    // Auto-calibrate (requires kart to be still)
    _mpu.CalibrateAccel(6);
    _mpu.CalibrateGyro(6);
    
    log_i("IMU: Calibration complete.");
    return true;
}

ImuData ImuManager::update() {
    ImuData data = {0};
    int16_t ax, ay, az, gx, gy, gz;

    // Check if the connection is still alive before reading
    if (!_mpu.testConnection()) {
        log_e("IMU: I2C Bus crashed! Initiating Auto-Recovery...");
        
        // 1. Nuke the crashed I2C driver
        Wire.end();
        delay(2); 
        
        // 2. Restart the I2C hardware
        Wire.begin(_sda_pin, _scl_pin, 100000);
        delay(5);
        
        // 3. Re-initialize the MPU if it went to sleep
        _mpu.initialize();
        
        if (!_mpu.testConnection()) {
            log_e("IMU: Auto-Recovery failed this cycle.");
            return data; // Return zeros, we'll try again in 200ms
        } else {
            log_i("IMU: Bus recovered successfully.");
        }
    }

    _mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

    // 8G scale factor is 4096 LSB/g
    data.accelX = ax / 4096.0f;
    data.accelY = ay / 4096.0f;
    data.accelZ = az / 4096.0f;

    // 500 deg/s scale factor is 65.5 LSB/(deg/s)
    data.gyroX = gx / 65.5f;
    data.gyroY = gy / 65.5f;
    data.gyroZ = gz / 65.5f;

    // Vector magnitude
    data.gForce = sqrt(pow(data.accelX, 2) + pow(data.accelY, 2));

    return data;
}