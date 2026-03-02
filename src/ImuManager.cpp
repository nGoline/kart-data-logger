#include "ImuManager.h"

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

    // Cálculo da G-Force Resultante
    data.gForce = sqrt(pow(data.accelX, 2) + pow(data.accelY, 2) + pow(data.accelZ, 2));

    // Conversão para graus/s (Opcional, se quiser medir a rotação do kart)
    data.gyroX = gx / 131.0f;
    data.gyroY = gy / 131.0f;
    data.gyroZ = gz / 131.0f;

    return data;
}