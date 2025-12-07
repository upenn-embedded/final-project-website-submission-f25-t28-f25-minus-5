#ifndef ESP32_UART_H
#define ESP32_UART_H

#include <Arduino.h>

// A parsed strum command
struct StrumCommand {
    int gesture;   // 0 = down, 1 = up
    int chord;     // midi chord index
    int velocity;  // 0–127
};

// Low-level: return raw "<chord>|<gesture>" from ATmega
bool getFromAtmega(String &chord, String &gesture, int &velocity);

// // High-level: convert ATmega message → StrumCommand
// bool getStrumCommand(StrumCommand &cmd);

#endif
