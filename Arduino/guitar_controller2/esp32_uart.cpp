#include "esp32_uart.h"

static String rxBuffer = "";

// ----------------------
//  1. 读取 ATmega 原始数据
// ----------------------
bool getFromAtmega(String &chord, String &gesture, int &velocity)
{
    while (Serial1.available()) {

        char c = Serial1.read();
        rxBuffer += c;

        if (c == '\n') {

            rxBuffer.trim();

            // 找到第一段分隔符
            int sep1 = rxBuffer.indexOf('|');
            if (sep1 < 0) {
                rxBuffer = "";
                return false;
            }

            // 找到第二段分隔符
            int sep2 = rxBuffer.indexOf('|', sep1 + 1);
            if (sep2 < 0) {
                rxBuffer = "";
                return false;
            }
            chord   = rxBuffer.substring(0, sep1);
            gesture = rxBuffer.substring(sep1 + 1, sep2);

            // velocity substring → int
            velocity = rxBuffer.substring(sep2 + 1).toInt();

            rxBuffer = "";
            return true;
        }
    }

    return false;
}

// -------------------------
//  2. 高级 API：解析成指令
// -------------------------
// bool getStrumCommand(StrumCommand &cmd)
// {
//     String chordStr, gestureStr;

//     // 如果没有完整帧，返回 false
//     if (!getFromAtmega(chordStr, gestureStr))
//         return false;

//     // 字符串 → int
//     cmd.chord   = chordStr.toInt();     // 例如 "4"
//     cmd.gesture = gestureStr.toInt();   // 例如 "0" (down)
    
//     // 如果 ATmega 不发送力度，可以设默认
//     cmd.velocity = 110;                 // 默认力度 or 自动推算

//     // 在这里你也可以做更复杂解析：
//     // - 根据 chord编号 → 获取音程
//     // - 根据 gesture → 选择扫弦方向
//     // - 根据传感器 → 算力度

//     return true;
// }
