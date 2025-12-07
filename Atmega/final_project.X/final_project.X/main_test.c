#include <avr/io.h>
#include <util/delay.h>
#include "./avr-printf-main/uart.h"
#include "./Keypad_detection/keypad.h"
#include "./Atmega2esp32/uart_protocol.h"
#include "./imu/imu_guitar.h"    // IMU API ???
#include "./avr-printf-main/uart.h"          // ???

#define IMU_ADDR 0x6B      // IMU I2C Address

int main(void)
{
    uart_init();
    keypad_init();
    GuitarIMU_init(IMU_ADDR);  // ??? GuitarIMU API
    uart_protocol_init();
    
    while (1)
    {
        /** get chord and additional func---- **/
        char key = keypad_scan();
        keypad_scan_group_buttons();
        if (key) {
            keypad_process(key);
        }
        const char* chord = keypad_get_chord();
//        uint8_t TBD = keypad_get_autoplay_state(); //TBD

        /** IMU gesture **/
        uint8_t strum_velocity = 0;
        const char* gesture = GuitarIMU_getStrum(&strum_velocity);
        
        
        if (gesture) {
            send_chord_gesture(chord, gesture, strum_velocity);
            printf("Chord=%s,  Gesture=%s, Velocity=%d\r\n",chord, gesture, strum_velocity); 
        }
    }
}
