#ifndef IMU_H
#define IMU_H

#include <stdint.h>

// SparkFun LSM6DS0 ? address depends on SA0 pin
// If SA0 is HIGH (often the case), address = 0x6B
#define LSM6DS0_ADDR 0x6B

void imu_init(void);
uint8_t imu_check_connection(void);
void imu_read_accel(int16_t *x, int16_t *y, int16_t *z);

#endif
