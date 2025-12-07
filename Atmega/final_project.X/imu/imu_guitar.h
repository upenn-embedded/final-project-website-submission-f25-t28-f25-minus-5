// #ifndef IMU_GUITAR_H
// #define IMU_GUITAR_H

// #include <stdint.h>

// void GuitarIMU_init(uint8_t addr);

// int16_t GuitarIMU_readAccX();
// int16_t GuitarIMU_readAccY();
// int16_t GuitarIMU_readAccZ();

// int16_t GuitarIMU_readGyroX();
// int16_t GuitarIMU_readGyroY();
// int16_t GuitarIMU_readGyroZ();

// void GuitarIMU_readAll(int16_t* accX, int16_t* accY, int16_t* accZ,
//                        int16_t* gyroX, int16_t* gyroY, int16_t* gyroZ);

// #endif
#ifndef IMU_GUITAR_H
#define IMU_GUITAR_H

#include <stdint.h>

void GuitarIMU_init(uint8_t addr);

int16_t GuitarIMU_readAccX();
int16_t GuitarIMU_readAccY();
int16_t GuitarIMU_readAccZ();

int16_t GuitarIMU_readGyroX();
int16_t GuitarIMU_readGyroY();
int16_t GuitarIMU_readGyroZ();

void GuitarIMU_readAll(int16_t* accX, int16_t* accY, int16_t* accZ,
                       int16_t* gyroX, int16_t* gyroY, int16_t* gyroZ);

/**
 * @brief 实时读取 IMU 陀螺仪数据并运行扫弦检测状态机。
 * @return const char* 返回检测到的扫弦方向 ("DOWN" 或 "UP")，如果未检测到则返回 NULL。
 * @note 此函数是 **非阻塞** 的，它只进行一次 IMU 读取和状态机检查。
 */
const char* GuitarIMU_getStrum(uint8_t* velocity_out);

#endif // IMU_GUITAR_H