#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h> 
#include "./avr-printf-main/uart.h"          // ???
#include "./imu/imu_guitar.h"    // IMU API ???

#define IMU_ADDR 0x6B      // IMU I2C Address
#define HEARTBEAT_INTERVAL 50  // 50 * 100ms = 5?

int main(void)
{
    uart_init();
    GuitarIMU_init(IMU_ADDR);  // ??? GuitarIMU API
    printf("IMU Strum Detector ready and running...\r\n");

    _delay_ms(1000);
    uint32_t timestamp_ms = 0;
//    uint8_t heartbeat_counter = 0; // ?????
    
    int16_t ax,ay,az,gx,gy,gz;
    while (1)
    {
        GuitarIMU_readAll(&ax, &ay, &az,
                          &gx, &gy, &gz);
        
        // 3. ????
        // ??: ???, ??X, ??Y, ??Z, ??X, ??Y, ??Z
        printf("%ld,%d,%d,%d,%d,%d,%d;\r\n", 
                timestamp_ms, ax, ay, az, gx, gy, gz);
//        const char* gesture = GuitarIMU_getStrum(); 
//        if (gesture) {
//        printf("Gesture=%s\r\n", gesture); 
//        }
        timestamp_ms += 100;
        _delay_ms(100); // 100ms ????
        
        // 4. ????
//        heartbeat_counter++;
//        if (heartbeat_counter >= HEARTBEAT_INTERVAL) {
//            // ????????????
//            printf("[STATUS] Detector is running. Time: %lu ms\r\n", timestamp_ms);
//            heartbeat_counter = 0; // ?????
//        }
    }
}