#include <avr/io.h>
#include <stdio.h>
#include "./uart_protocol.h"

// =========================
// UART ????
// =========================

static void uart1_init(uint16_t ubrr) {
    UBRR1H = (unsigned char) (ubrr >> 8);
    UBRR1L = (unsigned char) ubrr;

    UCSR1B = (1 << RXEN1) | (1 << TXEN1); // enable rx + tx
    UCSR1C = (1 << UCSZ11) | (1 << UCSZ10); // 8N1
}

static void uart1_send_char(char c) {
    while (!(UCSR1A & (1 << UDRE1)));
    UDR1 = c;
}

static void uart1_send_string(const char* s) {
    while (*s)
        uart1_send_char(*s++);
}

// =========================
// ??????
// =========================

void uart_protocol_init() {
    // 19200 baud @ 16MHz ? UBRR = 51
    uart1_init(51);
}

void send_chord_gesture(const char* chord, const char* gesture, uint8_t strum_velocity) {
    char velocity_str[4];
    // uint8_t2char
    snprintf(velocity_str, sizeof(velocity_str), "%u", strum_velocity); 
    // ??? C#|FULL_UP\n
    uart1_send_string(chord);
    uart1_send_char('|');
    uart1_send_string(gesture);
    uart1_send_char('|');
    uart1_send_string(velocity_str);
    uart1_send_char('\n');
}
