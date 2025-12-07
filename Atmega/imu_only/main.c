//#define F_CPU 16000000UL
//#include <avr/io.h>
//#include <util/delay.h>
//#include <stdio.h>
//#include "../i2c/i2c.h"
//#include "../imu/imu.h"
//
//
//#define STRUM_DOWN_THRESHOLD  -7000
//#define STRUM_UP_THRESHOLD     7000
//
//void uart_init(void)
//{
//    UBRR0H = 0;
//    UBRR0L = 103; // 9600 baud for 16MHz
//    UCSR0B = (1<<TXEN0);
//    UCSR0C = (1<<UCSZ01)|(1<<UCSZ00);
//}
//
//void uart_tx(char c)
//{
//    while (!(UCSR0A & (1<<UDRE0)));
//    UDR0 = c;
//}
//
//void uart_print(const char *s)
//{
//    while (*s)
//        uart_tx(*s++);
//}
//
//int main(void)
//{
//    uart_init();
//    i2c_init();
//    imu_init();
//
//    uart_print("Air Guitar Ready!\r\n");
//
//    int16_t ax, ay, az;
//
//    while (1)
//    {
//        imu_read_accel(&ax, &ay, &az);
//
//        if (az < STRUM_DOWN_THRESHOLD)
//            uart_print("Strum Down!\r\n");
//
//        if (az > STRUM_UP_THRESHOLD)
//            uart_print("Strum Up!\r\n");
//
//        _delay_ms(10);
//    }
//}

#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include "../i2c/i2c.h"
#include "../imu/imu.h"

#define STRUM_UP_THRESHOLD  -14000
#define STRUM_DOWN_THRESHOLD     14000

// ---------- UART @ 9600 baud ----------

void uart_init(void)
{
    // 16 MHz, 9600 baud -> UBRR = 103
    UBRR0H = 0;
    UBRR0L = 103;

    UCSR0B = (1 << TXEN0);                   // enable TX
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);  // 8N1
}

void uart_tx(char c)
{
    while (!(UCSR0A & (1 << UDRE0)));
    UDR0 = c;
}

void uart_print(const char *s)
{
    while (*s)
    {
        uart_tx(*s++);
    }
}

// Simple int16 -> decimal string
void uart_print_int(int16_t v)
{
    char buf[7];  // -32768\0
    uint8_t i = 0;
    uint16_t n;

    if (v < 0)
    {
        uart_tx('-');
        n = (uint16_t)(-v);
    }
    else
    {
        n = (uint16_t)v;
    }

    if (n == 0)
    {
        uart_tx('0');
        return;
    }

    while (n > 0 && i < sizeof(buf))
    {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }

    while (i > 0)
    {
        uart_tx(buf[--i]);
    }
}

int main(void)
{
    uart_init();
    i2c_init();
    
    uart_print("Air Guitar Ready! Debug mode\r\n");
    
    // Check IMU connection
    uint8_t who_am_i = imu_check_connection();
    uart_print("WHO_AM_I: 0x");
    uart_print_int(who_am_i); 
    uart_print("\r\n");

    if (who_am_i != 0x68 && who_am_i != 0x69 && who_am_i != 0x6A && who_am_i != 0x6B && who_am_i != 0x6C) {
        uart_print("WARNING: Unexpected WHO_AM_I value!\r\n");
    }

    imu_init();

    int16_t ax, ay, az;
    uint8_t strum_state = 0; // 0=Idle, 1=Strummed
    uint8_t cooldown = 0;    // Debounce timer

    while (1)
    {
        imu_read_accel(&ax, &ay, &az);

        if (cooldown > 0)
        {
            cooldown--;
        }
        else if (strum_state == 0)
        {
            // STRUM UP Detection (Negative Acceleration)
            if (ay < STRUM_UP_THRESHOLD)
            {
                uart_print("Strum UP!\r\n");
                strum_state = 1;
                cooldown = 20; 
            }
            // STRUM DOWN Detection (Positive Acceleration)
            else if (ay > STRUM_DOWN_THRESHOLD)
            {
                uart_print("Strum DOWN!\r\n");
                strum_state = 1;
                cooldown = 20;
            }
        }
        else
        {
            // Wait for return to neutral
            // Hysteresis: wait until acceleration drops below 4000
            if (ay > -4000 && ay < 4000)
            {
                strum_state = 0;
            }
        }

        _delay_ms(10);
    }
}
