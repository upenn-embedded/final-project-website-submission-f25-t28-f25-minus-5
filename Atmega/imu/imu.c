#include "imu.h"
#include "../i2c/i2c.h"
#include <avr/io.h>

#define WHO_AM_I      0x0F
#define CTRL_REG6_XL  0x20
#define CTRL_REG1_G   0x10
#define OUT_X_L_XL    0x28

void imu_init(void)
{
    // LSM6DSO Init
    // CTRL1_XL (0x10) = 0x60 -> ODR 416Hz, 2g
    i2c_start(LSM6DS0_ADDR << 1);
    i2c_write(0x10);      // CTRL1_XL
    i2c_write(0x60);
    i2c_stop();

    // CTRL2_G (0x11) = 0x60 -> ODR 416Hz, 2000dps
    i2c_start(LSM6DS0_ADDR << 1);
    i2c_write(0x11);      // CTRL2_G
    i2c_write(0x60);
    i2c_stop();
    
    // Ensure BDU (Block Data Update) is enabled for atomic reading?
    // CTRL3_C (0x12) -> Bit 6 = BDU, Bit 2 = IF_INC (default 1)
    // Let's set BDU = 1, IF_INC = 1 -> 0x44
    i2c_start(LSM6DS0_ADDR << 1);
    i2c_write(0x12);      // CTRL3_C
    i2c_write(0x44);      // BDU=1, IF_INC=1
    i2c_stop();
}

uint8_t imu_check_connection(void)
{
    i2c_start(LSM6DS0_ADDR << 1);
    i2c_write(WHO_AM_I);
    i2c_stop();

    i2c_start((LSM6DS0_ADDR << 1) | 1);
    uint8_t who_am_i = i2c_read_nack();
    i2c_stop();

    return who_am_i;
}

void imu_read_accel(int16_t *x, int16_t *y, int16_t *z)
{
    i2c_start((LSM6DS0_ADDR << 1));
    // For LSM6DSO, auto-increment is controlled by CTRL3_C, not the MSB of register address.
    // So we just send the base address 0x28.
    i2c_write(OUT_X_L_XL); 
    i2c_stop();

    i2c_start((LSM6DS0_ADDR << 1) | 1);

    uint8_t xl = i2c_read_ack();
    uint8_t xh = i2c_read_ack();
    uint8_t yl = i2c_read_ack();
    uint8_t yh = i2c_read_ack();
    uint8_t zl = i2c_read_ack();
    uint8_t zh = i2c_read_nack();

    *x = (int16_t)(xh << 8 | xl);
    *y = (int16_t)(yh << 8 | yl);
    *z = (int16_t)(zh << 8 | zl);

    i2c_stop();
}
