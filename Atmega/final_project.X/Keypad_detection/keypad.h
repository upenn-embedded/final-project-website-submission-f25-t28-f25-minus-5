#ifndef KEYPAD_H
#define KEYPAD_H

#include <stdint.h>
// Keypad
#define ROW_DDR   DDRD
#define ROW_PORT  PORTD
#define ROW_MASK  0xF0  // PD4-PD7
#define COL_DDR   DDRB
#define COL_PORT  PORTB
#define COL_PIN   PINB
#define COL_MASK  0x07  // PB0-PB2
//Button
// Group Buttons: PC0, PC1
#define BTN_DDR   DDRC
#define BTN_PORT  PORTC
#define BTN_PIN   PINC
#define BTN1_MASK 0x01  // PC0
#define BTN2_MASK 0x02  // PC1
#define BTN3_MASK 0x04  // PC2
#define BTN4_MASK 0x08  // PC3

void keypad_init(void);
char keypad_scan(void);

void keypad_scan_group_buttons(void);
uint8_t keypad_get_autoplay_state();

void keypad_process(char key);
const char* keypad_get_chord(void);

#endif
