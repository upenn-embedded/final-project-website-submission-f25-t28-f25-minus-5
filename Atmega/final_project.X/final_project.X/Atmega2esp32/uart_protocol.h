#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

void uart_protocol_init();
void send_chord_gesture(const char* chord, const char* gesture, uint8_t strum_velocity);

#endif
