#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include "./avr-printf-main/uart.h"
#include "./Atmega2esp32/uart_protocol.h"

int main(void)
{
    uart_protocol_init();

    while (1)
    {
        send_chord_gesture("D#", "FULL_UP");

        _delay_ms(20);
    }
}
