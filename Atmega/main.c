/*
  Air-guitar project
  Functions: Keypad scan for chord select, IMU for strum detection
  MCU: ATmega328PB
  UART: 9600 baud for debug
  Keypad layout (3x4):
    1 2 3
    4 5 6
    7 8 9
    * 0 #

  Pinouts:
    Rows (Outputs): PD4, PD5, PD6, PD7
    Cols (Inputs):  PB0, PB1, PB2
    Group Buttons:  PC0 (Group 1), PC1 (Group 2)
*/

// Libraries
#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <stdio.h>
#include <stdlib.h>
#include "i2c/i2c.h"
#include "imu/imu.h"

#define STRUM_UP_THRESHOLD  -14000
#define STRUM_DOWN_THRESHOLD     14000

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

// Keypad Pin Definitions
// Rows: PD4-PD7
#define ROW_DDR   DDRD
#define ROW_PORT  PORTD
#define ROW_PIN   PIND
#define ROW_MASK  0xF0  // PD4-PD7

// Cols: PB0-PB2
#define COL_DDR   DDRB
#define COL_PORT  PORTB
#define COL_PIN   PINB
#define COL_MASK  0x07  // PB0-PB2

// Group Buttons: PC0, PC1
// Other Buttons: PC2 (AutoMode), PC3 (PalmMute)
#define BTN_DDR   DDRC
#define BTN_PORT  PORTC
#define BTN_PIN   PINC
#define BTN1_MASK 0x01  // PC0 - Group 1
#define BTN2_MASK 0x02  // PC1 - Group 2
#define BTN3_MASK 0x04  // PC2 - AutoMode
#define BTN4_MASK 0x08  // PC3 - PalmMute

const char key_map[4][3] = {
    {'1','2','3'},
    {'4','5','6'},
    {'7','8','9'},
    {'*','0','#'}
};

/* Two groups of 12 chords */
const char* chord_map[2][12] = {
    {   // Group 1: majors + dominant 7ths (0..11)
        "C",  "G",  "D",  "A",  "E",  "F",
        "C7", "G7", "D7", "A7", "E7", "B7"
    },
    {   // Group 2: minors + sus & dim (12..23)
        "Am",   "Em",   "Dm",   "Bm",   "F#m",  "Gm",
        "Dsus4","Gsus4","Asus4","Esus4","Bdim","F#dim"
    }
};


/* 0 = group 1, 1 = group 2 */
uint8_t current_group = 0;

/* Status flags for additional modes */
uint8_t automode_enabled = 0;   // 0 = OFF, 1 = ON
uint8_t palmmute_enabled = 0;   // 0 = OFF, 1 = ON

char next_chord[10] = "C"; // Default to C

char scan_keypad(void) {
    for (uint8_t r = 0; r < 4; r++) {
        // Row pins are PD4..PD7.
        // To drive row r low (where r=0..3), we need to clear bit (r+4).
        // All other rows should be high (or input pullup, but here we drive high).
        
        // Set all rows high first
        ROW_PORT |= ROW_MASK; 
        // Set row (r+4) low
        ROW_PORT &= ~(1 << (r + 4));

        _delay_us(5);

        // Read columns (PB0..PB2)
        uint8_t cols = (COL_PIN & COL_MASK);
        
        // If any column is low, a button is pressed
        if (cols != COL_MASK) {
            _delay_ms(20);  // debounce
            cols = (COL_PIN & COL_MASK);
            for (uint8_t c = 0; c < 3; c++) {
                if (!(cols & (1 << c))) {
                    // Found key at row r, col c
                    
                    // Reset rows to high
                    ROW_PORT |= ROW_MASK;
                    return key_map[r][c];
                }
            }
        }
    }
    
    // Reset rows to high (idle state)
    ROW_PORT |= ROW_MASK;
    return '\0';
}

void scan_group_buttons(void) {
    // Buttons are on PC0-PC3, active low (pull-ups enabled)
    uint8_t btns = (BTN_PIN & (BTN1_MASK | BTN2_MASK | BTN3_MASK | BTN4_MASK));
    
    // Check Button 1 (PC0) - Group 1
    if (!(btns & BTN1_MASK)) {
        _delay_ms(20); // debounce
        if (!(BTN_PIN & BTN1_MASK)) {
            if (current_group != 0) {
                current_group = 0;
                char buf[64];
                snprintf(buf, sizeof(buf), "Switched to chord group %u\r\n", (unsigned)(current_group + 1));
                uart_puts(buf);
            }
        }
    }
    
    // Check Button 2 (PC1) - Group 2
    if (!(btns & BTN2_MASK)) {
        _delay_ms(20); // debounce
        if (!(BTN_PIN & BTN2_MASK)) {
            if (current_group != 1) {
                current_group = 1;
                char buf[64];
                snprintf(buf, sizeof(buf), "Switched to chord group %u\r\n", (unsigned)(current_group + 1));
                uart_puts(buf);
            }
        }
    }
    
    // Check Button 3 (PC2) - AutoMode Toggle
    if (!(btns & BTN3_MASK)) {
        _delay_ms(20); // debounce
        if (!(BTN_PIN & BTN3_MASK)) {
            // Only toggle if automode is currently OFF
            if (!automode_enabled) {
                automode_enabled = 1;
                uart_puts("AutoMode: ON\r\n");
            }
            // If automode is ON, do nothing (pressing the auto button again doesn't disable it)
        }
    }
    
    // Check Button 4 (PC3) - PalmMute Toggle
    if (!(btns & BTN4_MASK)) {
        _delay_ms(20); // debounce
        if (!(BTN_PIN & BTN4_MASK)) {
            palmmute_enabled = !palmmute_enabled;  // Toggle
            char buf[64];
            snprintf(buf, sizeof(buf), "PalmMute: %s\r\n", palmmute_enabled ? "ON" : "OFF");
            uart_puts(buf);
        }
    }
}

void process_key(char key) {
    if (key == '\0') return;

    // If automode is enabled, disable it when a keypad key is pressed
    if (automode_enabled) {
        automode_enabled = 0;
        uart_puts("AutoMode: OFF\r\n");
    }

    char buf[64];
    int index = -1;
    
    // Map 3x4 keys to 0-11 index
    switch (key) {
        case '1': index = 0; break;
        case '2': index = 1; break;
        case '3': index = 2; break;
        case '4': index = 3; break;
        case '5': index = 4; break;
        case '6': index = 5; break;
        case '7': index = 6; break;
        case '8': index = 7; break;
        case '9': index = 8; break;
        case '*': index = 9; break;
        case '0': index = 10; break;
        case '#': index = 11; break;
        default: index = -1; break;
    }

    if (index >= 0) {
        snprintf(next_chord, sizeof(next_chord), "%s", chord_map[current_group][index]);
        snprintf(buf, sizeof(buf),
                 "Key pressed: %c  ->  Next chord (group %u): %s\r\n",
                 key, (unsigned)(current_group + 1), next_chord);
    } else {
        snprintf(buf, sizeof(buf), "Key pressed: %c  ->  Invalid key\r\n", key);
    }
    uart_puts(buf);
}

int main(void) {
    uart_init();
    i2c_init();

    uart_puts("Air-Guitar Combined Init\r\n");

    // Check IMU connection
    uint8_t who_am_i = imu_check_connection();
    char buf[64];
    snprintf(buf, sizeof(buf), "WHO_AM_I: 0x%02X\r\n", who_am_i);
    uart_puts(buf);

    if (who_am_i != 0x68 && who_am_i != 0x69 && who_am_i != 0x6A && who_am_i != 0x6B && who_am_i != 0x6C) {
        uart_puts("WARNING: Unexpected WHO_AM_I value!\r\n");
    }

    imu_init();

    // --- GPIO Initialization ---
    
    // Rows (PD4-PD7): Output, start High
    ROW_DDR |= ROW_MASK;
    ROW_PORT |= ROW_MASK;

    // Cols (PB0-PB2): Input, Pull-up
    COL_DDR &= ~COL_MASK;
    COL_PORT |= COL_MASK;

    // Group Buttons (PC0-PC3): Input, Pull-up
    BTN_DDR &= ~(BTN1_MASK | BTN2_MASK | BTN3_MASK | BTN4_MASK);
    BTN_PORT |= (BTN1_MASK | BTN2_MASK | BTN3_MASK | BTN4_MASK);

    int16_t ax, ay, az;
    uint8_t strum_state = 0; // 0=Idle, 1=Strummed
    uint8_t cooldown = 0;    // Debounce timer

    while (1) {
        // 1. Scan Group Buttons
        scan_group_buttons();

        // 2. Scan Keypad
        char key = scan_keypad();
        if (key) {
            process_key(key);
        }

        // 3. Read IMU
        imu_read_accel(&ax, &ay, &az);

        // 4. Detect Strum
        if (cooldown > 0)
        {
            cooldown--;
        }
        else if (strum_state == 0)
        {
            // STRUM UP Detection (Negative Acceleration)
            if (ay < STRUM_UP_THRESHOLD)
            {
                snprintf(buf, sizeof(buf), "Strum UP! Playing: %s\r\n", next_chord);
                uart_puts(buf);
                strum_state = 1;
                cooldown = 20; 
            }
            // STRUM DOWN Detection (Positive Acceleration)
            else if (ay > STRUM_DOWN_THRESHOLD)
            {
                snprintf(buf, sizeof(buf), "Strum DOWN! Playing: %s\r\n", next_chord);
                uart_puts(buf);
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

        _delay_ms(10); // Faster loop for IMU responsiveness
    }
}
