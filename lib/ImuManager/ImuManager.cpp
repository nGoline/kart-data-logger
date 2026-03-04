#include "ImuManager.h"

ImuManager::ImuManager() : _mpu(0x68) {}

bool ImuManager::begin(int sda, int scl) {
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

void ImuManager::startTask(QueueHandle_t telemetryQueue) {
    xTaskCreatePinnedToCore([](void* p){
        ImuManager* self = (ImuManager*)p;
        QueueHandle_t q = (QueueHandle_t)pvTaskGetThreadLocalStoragePointer(NULL, 0); // Not ideal, passing via struct is better
        
        for (;;) {
            ImuData rd = self->update();
            
            // Filter out gravity (1.0g baseline)
            self->_gFiltered = self->_gAlpha * rd.gForce + (1.0f - self->_gAlpha) * self->_gFiltered;
            float dynG = self->_gFiltered - 1.0f; 
            if (dynG < 0.05f) dynG = 0.0f; // Noise floor

            // Instead of extern volatile, we would ideally update a local struct 
            // and send to the queue if the logger needs to broadcast it.
            
            vTaskDelay(pdMS_TO_TICKS(10)); // 100Hz IMU sampling
        }
    }, "IMU_Task", 4096, this, 2, NULL, 1);
}