#include "./keypad.h"
#include <avr/io.h>
#include <util/delay.h>
#include <string.h>
#include "../avr-printf-main/uart.h"

static uint8_t current_group = 0;
static char next_chord[10];
static uint8_t autoplay_state = 0;

static const char key_map[4][3] = {
    {'1','2','3'},
    {'4','5','6'},
    {'7','8','9'},
    {'*','0','#'}
};

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

void keypad_init()
{
    //3X4
    // Rows (PD4-PD7): Output, start High
    ROW_DDR |= ROW_MASK;
    ROW_PORT |= ROW_MASK;
    // Cols (PB0-PB2): Input, Pull-up
    COL_DDR &= ~COL_MASK;
    COL_PORT |= COL_MASK;

    //1X4
    // Group buttons PC0?PC1 ?? + ??
    BTN_DDR &= ~(BTN1_MASK | BTN2_MASK | BTN3_MASK | BTN4_MASK);
    BTN_PORT |= (BTN1_MASK | BTN2_MASK | BTN3_MASK | BTN4_MASK);
}

char keypad_scan()
{
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
                    // Reset rows to high
                    autoplay_state = 0;
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

void keypad_scan_group_buttons()
{
    uint8_t btns = BTN_PIN;

    // Button1 -> group 0
    if (!(btns & BTN2_MASK)) {
        _delay_ms(15); 
        if (!(BTN_PIN & BTN2_MASK)) {   // check again
            current_group = 0;
            // printf("Button1 pressed, current group: %d\r\n", current_group);
        }
    }

    // Button2 -> group 1
    if (!(btns & BTN1_MASK)) {
        _delay_ms(15);
        if (!(BTN_PIN & BTN1_MASK)) {
            current_group = 1;
            // printf("Button2 pressed, current group: %d\r\n", current_group);
        }
    }
    

    // Button3 -> autoplay
    if (!(btns & BTN3_MASK)) {
        _delay_ms(15);
        if (!(BTN_PIN & BTN3_MASK)) {
            autoplay_state = 1;
            strcpy(next_chord, "AUTOKEY");
        }
    }

    // Button4 -> group 1
    if (!(btns & BTN4_MASK)) {
        _delay_ms(15);
        if (!(BTN_PIN & BTN4_MASK)) {
            // todo 
        }
    }
}

void keypad_process(char key)
{
    if (!key) return;

    int index = -1;
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
        strcpy(next_chord, chord_map[current_group][index]);
    }
}

const char* keypad_get_chord()
{
    return next_chord;
}

// uint8_t keypad_get_autoplay_state()
// {
//     return autoplay_state;
// }