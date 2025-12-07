#include "esp32_uart.h"

static String rxBuffer = "";

// ----------------------
//  1. 读取 ATmega 原始数据
// ----------------------
// 新格式： chord|gesture|velocity|volume\n
// 旧格式： chord|gesture|velocity\n   （volume 自动=100）
bool getFromAtmega(String &chord, String &gesture, int &velocity, int &volume)
{
    while (Serial1.available()) {

        char c = Serial1.read();

        // 忽略回车
        if (c == '\r') {
            continue;
        }

        rxBuffer += c;

        if (c == '\n') {
            // 一帧结束
            String line = rxBuffer;
            rxBuffer = "";      // 清空 buffer，准备下一帧

            line.trim();
            if (line.length() == 0) {
                return false;
            }

            // 找到分隔符
            int sep1 = line.indexOf('|');
            if (sep1 < 0) {
                return false;
            }

            int sep2 = line.indexOf('|', sep1 + 1);
            if (sep2 < 0) {
                return false;
            }

            // 尝试找第三个分隔符（volume）
            int sep3 = line.indexOf('|', sep2 + 1);

            chord   = line.substring(0, sep1);
            gesture = line.substring(sep1 + 1, sep2);

            if (sep3 < 0) {
                // 旧格式： chord|gesture|velocity
                velocity = line.substring(sep2 + 1).toInt();
                volume   = 100;  // 默认 100%
            } else {
                // 新格式： chord|gesture|velocity|volume
                velocity = line.substring(sep2 + 1, sep3).toInt();
                volume   = line.substring(sep3 + 1).toInt();
            }

            // 简单 clamp 一下，避免奇怪值
            if (velocity < 0)   velocity = 0;
            if (velocity > 127) velocity = 127;

            if (volume < 0)     volume = 0;
            if (volume > 100)   volume = 100;

            return true;
        }
    }

    return false;
}
