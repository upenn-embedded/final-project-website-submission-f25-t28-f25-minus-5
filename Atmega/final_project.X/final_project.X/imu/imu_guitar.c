#include "./imu_guitar.h"
#include "./new_i2c.h"
#include "./imu.h"
#include <stdio.h>    // For printf
#include <stdlib.h>   // For abs()
#include <stdbool.h>  // For bool types
#include <avr/interrupt.h>
#include "../avr-printf-main/uart.h"

static uint8_t address;

void GuitarIMU_init(uint8_t addr) {
    address = addr;
    NewI2C_init(1); //Initialize I2C, input 0 used to ignore the ERROR() function
    cli();
    IMU_init(address); //initial imu using the found I2C address from the 
    // previous task
    sei();
}

static int16_t read16(uint8_t reg) {
    uint8_t data[2];
    // ???? I2C ????
    NewI2C_readCompleteStream(data, address, reg, 2);
    // LSM6DSO ? little-endian?low ??
    return (int16_t) ((data[1] << 8) | data[0]);
}

// --------------------- ??? ---------------------

int16_t GuitarIMU_readAccX() {
    return read16(0x28);
}

int16_t GuitarIMU_readAccY() {
    return read16(0x2A);
}

int16_t GuitarIMU_readAccZ() {
    return read16(0x2C);
}

// --------------------- ??? ---------------------

int16_t GuitarIMU_readGyroX() {
    return read16(0x22);
}

int16_t GuitarIMU_readGyroY() {
    return read16(0x24);
}

int16_t GuitarIMU_readGyroZ() {
    return read16(0x26);
}

void GuitarIMU_readAll(int16_t* ax, int16_t* ay, int16_t* az,
        int16_t* gx, int16_t* gy, int16_t* gz) {
    *ax = GuitarIMU_readAccX();
    *ay = GuitarIMU_readAccY();
    *az = GuitarIMU_readAccZ();
    *gx = GuitarIMU_readGyroX();
    *gy = GuitarIMU_readGyroY();
    *gz = GuitarIMU_readGyroZ();
}

/**
 * @brief ? GZ ?????? 0-127 ??????
 * @param raw_gz ??? Z ?????
 * @return uint8_t 0-127 ?????
 */

// =================================================================
//                 ?????? (Strumming Detection Logic)
// =================================================================

// --- GZ ???? (Directional Thresholds) ---
#define GZ_BIAS                     416         // GZ ????????? (??)
#define GX_BIAS                     -1000         // GZ ????????? (??)
// Angular
#define POSITIVE_THRESHOLD_RAW      10000       // GZ 14->15->13->10000
#define NEGATIVE_THRESHOLD_RAW      -10000      // GZ -16->14->12->10
#define GX_MAX      20000      // GX 13000->20000
#define POSITIVE_THRESHOLD_GY      5000       // GY 8000-5000
#define NEGATIVE_THRESHOLD_GY      -5000      // GY
//Accelerate
#define AX_MIN      -6000      // GX -1000->-10000->-6000
// --- ???? (Kinetic Filter Threshold) ---
// ???????????? (? 306845172UL)
#define SQUARED_MAGNITUDE_THRESHOLD 650000000UL 

// --- ???/???? ---
// GZ ????????? (BIAS) ?????????????
#define RESET_THRESHOLD             4000         



typedef enum {
    IDLE, // ?????????
    SWING_DOWN, // ?????????????
    SWING_UP, // ?????????????
    PALM_MUTE
} StrumState;

static StrumState current_state = IDLE;
// Used to find max mag_sq
static uint32_t current_mag_sq_peak = 0;
// ??????????? (DOWN/UP)??????????????
static const char* pending_strum_direction = NULL; 
/**
 * @brief ?????????????? (????/???????)
 */
uint32_t calculate_gyro_mag_squared(int16_t raw_gx, int16_t raw_gy, int16_t raw_gz) {
    // ????? int32_t ??????? int16_t ???????? uint32_t?
    uint32_t gx_sq = (uint32_t) ((int32_t) raw_gx * raw_gx);
    uint32_t gy_sq = (uint32_t) ((int32_t) raw_gy * raw_gy);
    uint32_t gz_sq = (uint32_t) ((int32_t) raw_gz * raw_gz);

    return gx_sq + gy_sq + gz_sq;
}

// calculate velocity 
#define MAG_SQ_MIN_VELOCITY         800000000UL  // 0.8e9 - reflected to vel 0
#define MAG_SQ_MAX_VELOCITY         2500000000UL     // 2.5e9 - reflected to vel 127
#define MAG_SQ_RANGE_VELOCITY       (MAG_SQ_MAX_VELOCITY - MAG_SQ_MIN_VELOCITY)
uint8_t map_velocity(uint32_t peak_mag_sq) {
    if (peak_mag_sq <= MAG_SQ_MIN_VELOCITY) {
        return 0;
    }
    if (peak_mag_sq >= MAG_SQ_MAX_VELOCITY) {
        return 127;
    }

    // vel = ((current - min) * max) / range
    //  uint64_t ?????????? (800M * 127 < 2^32)
    uint64_t numerator = (uint64_t)peak_mag_sq - MAG_SQ_MIN_VELOCITY;
    uint32_t velocity = (uint32_t)((numerator * 127) / MAG_SQ_RANGE_VELOCITY); 

    // 4. cap vel to 127
    if (velocity > 127) {
        return 127;
    }
    return (uint8_t)velocity;
}
/**
 * @brief ?? GZ ???????????????
 */
const char* GuitarIMU_getStrum(uint8_t* velocity_out) {
    
    int16_t raw_ax = GuitarIMU_readAccX();
    int16_t raw_gx = GuitarIMU_readGyroX();
    int16_t raw_gy = GuitarIMU_readGyroY();
    int16_t raw_gz = GuitarIMU_readGyroZ(); // ??? GZ ??????
    //    printf("%d,%d,%d;\n",raw_gx, raw_gy, raw_gz);
    // 1. ??????????? (Squared Magnitude)
    uint32_t mag_sq = calculate_gyro_mag_squared(raw_gx, raw_gy, raw_gz);
    // 2. ??????? GZ ?????????
    int16_t gz_diff = raw_gz - GZ_BIAS;
    int16_t gx_diff = raw_gx - GX_BIAS;
    
    const char* detected_strum = NULL; // ?????????
    
    if (current_state != IDLE) {
        if (mag_sq > current_mag_sq_peak) {
            current_mag_sq_peak = mag_sq;
        }
    }
    
//     switch (current_state) {
//         case IDLE:
//             // ????: 1. ????? AND 2. ???????
//             if (mag_sq > SQUARED_MAGNITUDE_THRESHOLD && abs(raw_gx) < GX_MAX && raw_ax>AX_MIN ) {
//                 // ???? -> DOWNSTROKE (???) - ????
//                 if (raw_gz < NEGATIVE_THRESHOLD_RAW && raw_gy > POSITIVE_THRESHOLD_GY ) {
//                     pending_strum_direction = "STRUM_DOWN";
//                     current_state = SWING_DOWN;
//                     current_mag_sq_peak = mag_sq;
// //                    *velocity_out = map_velocity(raw_gz);
//                     // ???? -> UPSTROKE (???) - ????
//                 } else if (raw_gz > POSITIVE_THRESHOLD_RAW && raw_gy < NEGATIVE_THRESHOLD_RAW ) {
//                     pending_strum_direction = "STRUM_UP";
//                     current_state = SWING_UP;
//                     current_mag_sq_peak = mag_sq;
// //                    *velocity_out = map_velocity(raw_gz);
//                 }
//             }
//             break;
//         case SWING_DOWN:
//         case SWING_UP:
//             // ?? GZ ?????? BIAS ???????????
//             if (abs(gz_diff) < RESET_THRESHOLD) {
//                 current_state = IDLE;
//                 // trigger strum & calculate velocity
//                 detected_strum = pending_strum_direction; 
//                 *velocity_out = map_velocity(current_mag_sq_peak);
//                 // reset
//                 current_mag_sq_peak = 0;
//                 pending_strum_direction = NULL;
//             }
//             break;
//     }
switch (current_state) {
        case IDLE:
            // ????: 1. ????? AND 2. ???????
            if (mag_sq > SQUARED_MAGNITUDE_THRESHOLD && abs(raw_gx) < GX_MAX && raw_ax>AX_MIN ) {
                // ???? -> DOWNSTROKE (???) - ????
                if (raw_gz < NEGATIVE_THRESHOLD_RAW && raw_gy > POSITIVE_THRESHOLD_GY ) {
                    pending_strum_direction = "STRUM_DOWN";
                    current_state = SWING_DOWN;
                    current_mag_sq_peak = mag_sq;
//                    *velocity_out = map_velocity(raw_gz);
                    // ???? -> UPSTROKE (???) - ????
                } else if (raw_gz > POSITIVE_THRESHOLD_RAW && raw_gy < NEGATIVE_THRESHOLD_RAW ) {
                    pending_strum_direction = "STRUM_UP";
                    current_state = SWING_UP;
                    current_mag_sq_peak = mag_sq;
//                    *velocity_out = map_velocity(raw_gz);
                } 
            } else if (mag_sq > SQUARED_MAGNITUDE_THRESHOLD && raw_gx > GX_MAX + 3500 && raw_gz < 5000 ) {
                    pending_strum_direction = "PALM_MUTE";
                    current_state = PALM_MUTE;
//                    *velocity_out = map_velocity(raw_gz);
                }
            break;
        case SWING_DOWN:
        case SWING_UP:
            // ?? GZ ?????? BIAS ???????????
            if (abs(gz_diff) < RESET_THRESHOLD) {
                current_state = IDLE;
                // trigger strum & calculate velocity
                detected_strum = pending_strum_direction; 
                *velocity_out = map_velocity(current_mag_sq_peak);
                // reset
                current_mag_sq_peak = 0;
                pending_strum_direction = NULL;
            }
            break;
        case PALM_MUTE:  
          if (abs(gx_diff) < RESET_THRESHOLD) {
                current_state = IDLE;
                // trigger strum & calculate velocity
                detected_strum = pending_strum_direction; 
                // reset
                pending_strum_direction = NULL;
            }
            break;
    }
    return detected_strum;
}
