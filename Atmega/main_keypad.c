/*
  Keypad scan + chord select for air-guitar project
  MCU: ATmega328PB
  UART: 9600 baud for debug
  Keypad layout:
    1 2 3 A
    4 5 6 B
    7 8 9 C
    * 0 # D
*/

#define F_CPU 16000000UL  // adjust to your clock frequency
#include <avr/io.h>
#include <util/delay.h>
#include <stdio.h>
#include <stdlib.h>

// UART initialization
void uart_init(void) {
    uint16_t ubrr = F_CPU/16/9600 - 1;
    UBRR0H = (uint8_t)(ubrr >> 8);
    UBRR0L = (uint8_t)(ubrr & 0xFF);
    UCSR0B = (1<<TXEN0);
    UCSR0C = (1<<UCSZ01)|(1<<UCSZ00);  // 8-bit, no parity, 1 stop
}

void uart_putc(char c) {
    while (!(UCSR0A & (1<<UDRE0)));
    UDR0 = c;
}

void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

#define ROW_DDR   DDRD
#define ROW_PORT  PORTD
#define ROW_PIN   PIND
#define COL_DDR   DDRC
#define COL_PORT  PORTC
#define COL_PIN   PINC

const char key_map[4][4] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};

/* Two groups of 12 chords */
const char* chord_map[2][12] = {
    {   // Group 1
        "C", "C#", "D", "D#",
        "E", "F", "F#", "G",
        "G#", "A", "A#", "B"
    },
    {   // Group 2 (example alt set, change as you like)
        "Cm", "C#m", "Dm", "D#m",
        "Em", "Fm", "F#m", "Gm",
        "G#m", "Am", "A#m", "Bm"
    }
};

/* 0 = group 1, 1 = group 2 */
uint8_t current_group = 0;

char next_chord[10] = {0};

char scan_keypad(void) {
    for (uint8_t r = 0; r < 4; r++) {
        // set row r output low, other rows high impedance
        ROW_DDR = (1<<r);
        ROW_PORT = ~(1<<r);   // drive only row r low
        _delay_us(5);
        uint8_t cols = (COL_PIN & 0x0F);
        if (cols != 0x0F) {
            // key press detected
            _delay_ms(20);  // debounce
            cols = (COL_PIN & 0x0F);
            for (uint8_t c = 0; c < 4; c++) {
                if (!(cols & (1<<c))) {
                    // key at [r][c]
                    // reset row outputs
                    ROW_DDR = 0x00;
                    ROW_PORT = 0xFF;
                    return key_map[r][c];
                }
            }
        }
        // reset row
        ROW_DDR = 0x00;
        ROW_PORT = 0xFF;
    }
    return '\0';
}

void process_key(char key) {
    if (key == '\0') return;

    char buf[64];

    // Handle group switching first
    if (key == '*') {
        current_group = 0;  // group 1
        snprintf(buf, sizeof(buf), "Switched to chord group %u\r\n", (unsigned)(current_group + 1));
        uart_puts(buf);
        return;
    } else if (key == '0') {
        current_group = 1;  // group 2
        snprintf(buf, sizeof(buf), "Switched to chord group %u\r\n", (unsigned)(current_group + 1));
        uart_puts(buf);
        return;
    }

    int index = -1;
    switch (key) {
        case '1': index = 0; break;
        case '2': index = 1; break;
        case '3': index = 2; break;
        case 'A': index = 3; break;
        case '4': index = 4; break;
        case '5': index = 5; break;
        case '6': index = 6; break;
        case 'B': index = 7; break;
        case '7': index = 8; break;
        case '8': index = 9; break;
        case '9': index = 10; break;
        case 'C': index = 11; break;
        default: index = -1; break;
    }

    if (index >= 0) {
        snprintf(next_chord, sizeof(next_chord), "%s", chord_map[current_group][index]);
        snprintf(buf, sizeof(buf),
                 "Key pressed: %c  ?  Next chord (group %u): %s\r\n",
                 key, (unsigned)(current_group + 1), next_chord);
    } else {
        snprintf(buf, sizeof(buf), "Key pressed: %c  ?  Invalid key\r\n", key);
    }
    uart_puts(buf);
}

int main(void) {
    uart_init();
    uart_puts("Air-Guitar Keypad Init\r\n");

    // keypad init
    ROW_DDR = 0x00;
    ROW_PORT = 0xFF;   // rows high
    COL_DDR = 0x00;
    COL_PORT = 0xFF;   // columns pull-ups

    while (1) {
        char key = scan_keypad();
        if (key) {
            process_key(key);
            // next_chord is set for later IMU swipe trigger
        }
        _delay_ms(50);
    }
}
