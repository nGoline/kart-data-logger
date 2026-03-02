#include "ImuManager.h"
#include <Arduino.h>

// Forward declare the centralized logging function defined in main.cpp
extern void logRemote(const char* format, ...);

ImuManager::ImuManager() : _mpu(0x68) {}

bool ImuManager::begin() {
    // Inicializa o chip
    _mpu.initialize();

    // Verifica a conexão
    if (!_mpu.testConnection()) {
        return false;
    }

    _mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_8); // Configura o Full Scale para o Kart (+/- 8G)
    _mpu.setFullScaleGyroRange(MPU6050_GYRO_FS_500); // 500 graus/s é mais que suficiente para o kart
    _mpu.setDLPFMode(MPU6050_DLPF_BW_20);            // Filtro Digital Passa-Baixa de 20Hz (Corta a vibração do motor)
    
    // Opcional: Calibração automática (deixe o sensor parado na bancada ao ligar)
    // Isso zera os erros residuais de fabricação.
    _mpu.CalibrateAccel(6);
    _mpu.CalibrateGyro(6);

    return true;
}

ImuData ImuManager::update() {
    ImuData data = {0};
    int16_t ax, ay, az, gx, gy, gz;

    // Leitura bruta dos registradores
    _mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

    // Conversão para G (Escala 8G = 4096 LSB/g)
    data.accelX = ax / 4096.0f;
    data.accelY = ay / 4096.0f;
    data.accelZ = az / 4096.0f;

    // Cálculo da G-Force Resultante (raw magnitude)
    data.gForce = sqrt(pow(data.accelX, 2) + pow(data.accelY, 2) + pow(data.accelZ, 2));

    // Conversão para graus/s (Opcional, se quiser medir a rotação do kart)
    data.gyroX = gx / 131.0f;
    data.gyroY = gy / 131.0f;
    data.gyroZ = gz / 131.0f;

    return data;
}

void ImuManager::startTask() {
    xTaskCreatePinnedToCore([](void* p){
        ImuManager* self = (ImuManager*)p;
        for (;;) {
            ImuData rd = self->update();
            extern volatile TelemetryData currentData;
            // Compute a smoothed dynamic G value (remove ~1g gravity baseline)
            self->_gFiltered = self->_gAlpha * rd.gForce + (1.0f - self->_gAlpha) * self->_gFiltered;
            float dynG = self->_gFiltered - 1.0f; // remove gravity
            if (dynG < 0.02f) dynG = 0.0f; // deadband to suppress noise
            currentData.gForce = dynG;
            currentData.gyroZ = rd.gyroZ;

            // Emit concise telemetry status for remote/serial monitoring
            if (currentData.hasFix) {
                logRemote("G:%.2f | V:%.1f km/h | S:%u\n",
                          currentData.gForce,
                          currentData.speed,
                          (unsigned int)currentData.sats);
            }

            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }, "IMU_Task", 4096, this, 2, NULL, 1);
}