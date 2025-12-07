#ifndef ESP32_UART_H
#define ESP32_UART_H

#include <Arduino.h>

// A parsed strum command
struct StrumCommand {
    int gesture;   // 0 = down, 1 = up
    int chord;     // midi chord index
    int velocity;  // 0–127
};

// ATmega → ESP32 协议：
// 推荐新格式：chord|gesture|velocity|volume\n
//   - chord   : String，比如 "C", "Am", "AUTOKEY"
//   - gesture : String，比如 "STRUM_UP", "STRUM_DOWN", "MUTE"
//   - velocity: 0..127
//   - volume  : 0..100   （主音量百分比）
//
// 兼容旧格式：chord|gesture|velocity\n
//   - 此时 volume 会被设为 100
//
bool getFromAtmega(String &chord, String &gesture, int &velocity, int &volume);

#endif
