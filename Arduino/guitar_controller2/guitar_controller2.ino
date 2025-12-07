#include <Arduino.h>
#include <ESP_I2S.h>
#include <math.h>
#include "esp32_uart.h"

// ================== I2S 引脚 ==================
#define I2S_LRC 17
#define I2S_BCLK 18
#define I2S_DIN 14

// ================== 合成 & 音色参数（集中调节区） ==================
#define NUM_STRINGS 3                           // 每个和弦用几根“虚拟弦”（先用 3 声部）
#define MAX_KS_DELAY 512                        // Karplus-Strong 最大延迟长度
#define MAX_SCHEDULED_PLUCKS (NUM_STRINGS * 4)  // 允许同时排队的 pluck 数 
        
// 基本音频
const int kSampleRate = 16000;  // 采样率，16k 足够 + 稳定

// 扫弦：每次 pluck 的衰减由 KS 自己和音量控制，不再固定持续时间
// 我们只控制“弦与弦之间的时间间隔”
const float kInterDelayMsSlow = 20.0f;  // 力度很小时，弦间延时（慢扫）
const float kInterDelayMsFast = 4.0f;   // 力度很大时，弦间延时（快扫）

// Karplus-Strong 衰减（越接近 1，尾巴越长）
const float kKsDecay = 0.994f;  // 0.992–0.996 可微调

// 拨弦噪声 RMS 基准
const float kBaseNoiseTargetRms = 0.20f;  // 基准 RMS
const float kVelRmsScaleMin = 0.5f;       // 力度最小时系数
const float kVelRmsScaleMax = 1.4f;       // 力度最大时系数

// 每根弦的 detune（cent），让和弦更宽一点
const float kDetuneCents[NUM_STRINGS] = { -3.0f, 0.0f, 3.0f };

// Tone shaping：频响和箱体感
const float kLpToneAlpha = 0.24f;  // 高频低通强度：0.2 偏暖，0.3 偏亮
const float kPresenceMix = 0.25f;  // 拨弦存在感 0~0.4
const float kBodyAlpha = 0.02f;    // 箱体共鸣低频滤波速度
const float kBodyMix = 0.25f;      // 箱体占比 0~0.4

// 总体增益
const float kOutputGain = 1.3f;  // 根据喇叭和耳朵来调

// ================== Karplus-Strong 结构体 ==================
struct KSString {
  float buffer[MAX_KS_DELAY];
  int length;
  int index;
  float decay;
  bool active;
};

KSString ksStrings[NUM_STRINGS];

// ================== 和声配置（C 调 4536251 用到的六个和弦） ==================
struct MidiChord {
  const char *name;
  const uint8_t *notes;
  size_t count;
};

// C 大调常用和弦
const uint8_t CmajNotes[] = { 60, 64, 67 };  // C4 E4 G4
const uint8_t GmajNotes[] = { 55, 59, 62 };  // G3 B3 D4
const uint8_t AminNotes[] = { 57, 60, 64 };  // A3 C4 E4
const uint8_t EminNotes[] = { 52, 55, 59 };  // E3 G3 B3
const uint8_t FmajNotes[] = { 53, 57, 60 };  // F3 A3 C4
const uint8_t DminNotes[] = { 50, 53, 57 };  // D3 F3 A3

// chord index 映射：0=C,1=Dm,2=Em,3=F,4=G,5=Am
enum {
  CHORD_C = 0,
  CHORD_Dm = 1,
  CHORD_Em = 2,
  CHORD_F = 3,
  CHORD_G = 4,
  CHORD_Am = 5,
};

MidiChord chords[] = {
  { "C", CmajNotes, sizeof(CmajNotes) / sizeof(uint8_t) },   // 0
  { "Dm", DminNotes, sizeof(DminNotes) / sizeof(uint8_t) },  // 1
  { "Em", EminNotes, sizeof(EminNotes) / sizeof(uint8_t) },  // 2
  { "F", FmajNotes, sizeof(FmajNotes) / sizeof(uint8_t) },   // 3
  { "G", GmajNotes, sizeof(GmajNotes) / sizeof(uint8_t) },   // 4
  { "Am", AminNotes, sizeof(AminNotes) / sizeof(uint8_t) },  // 5
};

const int NUM_CHORDS = sizeof(chords) / sizeof(chords[0]);

// ================== Strum 相关 ==================
enum StrumDirection {
  STRUM_DOWN = 0,  // 低音 → 高音
  STRUM_UP = 1     // 高音 → 低音
};

// 每个 strum 会拆成若干“string pluck”事件，按时间错开触发
struct ScheduledPluck {
  bool active;
  uint32_t triggerSample;  // 何时触发（全局 sample 计数）
  int chordIndex;          // 用哪个和弦
  int stringIndex;         // 和弦中的第几根“弦”（0..NUM_STRINGS-1）
  float velocityNorm;      // 0~1，力度
};

ScheduledPluck gPlucks[MAX_SCHEDULED_PLUCKS];

static uint32_t gSampleCounter = 0;  // 全局 sample 计数，用来做时间轴

// Tone shaping 状态（滤波器记忆）
struct ToneState {
  float lpTone;
  float lpBody;
};

ToneState toneState;

// ================== I2S 实例 ==================
I2SClass i2s;

// ================== 串口命令解析 ==================
static const int CMD_BUF_SIZE = 32;
char cmdBuf[CMD_BUF_SIZE];
int cmdLen = 0;

// 命令格式： <D/U> <chordIndex> <velocity>
// 例如：   D 4 110   → 下扫 G 和弦，力度 110
//         U 0 80    → 上扫 C 和弦，力度 80

// ================== 工具函数 ==================
float midiToFreq(uint8_t midi) {
  return 440.0f * powf(2.0f, ((int)midi - 69) / 12.0f);
}

// 初始化一根 KS 弦：平滑噪声 + RMS 归一化，根据力度设置目标 RMS
void initKSString(KSString &s, float freq, float decay, float targetRms) {
  if (freq <= 0.0f) {
    s.active = false;
    return;
  }
  int len = (int)((float)kSampleRate / freq + 0.5f);
  if (len < 2) len = 2;
  if (len > MAX_KS_DELAY) len = MAX_KS_DELAY;

  s.length = len;
  s.index = 0;
  s.decay = decay;
  s.active = true;

  float prev = 0.0f;
  float sumSq = 0.0f;

  for (int i = 0; i < len; ++i) {
    int r = random(-32768, 32767);
    float val = (float)r / 32768.0f;  // -1 ~ 1

    // 平滑一点，让起音更木、少毛刺
    val = 0.4f * val + 0.6f * prev;
    prev = val;

    s.buffer[i] = val;
    sumSq += val * val;
  }

  for (int i = len; i < MAX_KS_DELAY; ++i) {
    s.buffer[i] = 0.0f;
  }

  // RMS 归一化，让不同力度在“感知上”更线性
  if (sumSq > 1e-6f) {
    float rms = sqrtf(sumSq / (float)len);
    float scale = targetRms / rms;
    for (int i = 0; i < len; ++i) {
      s.buffer[i] *= scale;
    }
  }
}

float processKSString(KSString &s) {
  if (!s.active) return 0.0f;

  int i0 = s.index;
  int i1 = (s.index + 1);
  if (i1 >= s.length) i1 = 0;

  float y = 0.5f * (s.buffer[i0] + s.buffer[i1]);
  y *= s.decay;

  s.buffer[i0] = y;
  s.index = i1;

  return y;
}

// 启动单根弦的 pluck：给定 chordIndex, stringIndex, 力度
void startPluck(int chordIndex, int stringIndex, float velocityNorm) {
  if (chordIndex < 0 || chordIndex >= NUM_CHORDS) return;

  MidiChord &ch = chords[chordIndex];
  if (stringIndex < 0 || stringIndex >= (int)ch.count) return;
  if (stringIndex >= NUM_STRINGS) return;

  float freq = midiToFreq(ch.notes[stringIndex]);

  // 轻微 detune
  float detune_cents = kDetuneCents[stringIndex];
  float detune = powf(2.0f, detune_cents / 1200.0f);
  freq *= detune;

  // 根据力度放大 RMS：力度越大，targetRms 越高
  float v = constrain(velocityNorm, 0.0f, 1.0f);
  float velScale = kVelRmsScaleMin + (kVelRmsScaleMax - kVelRmsScaleMin) * v;
  float targetRms = kBaseNoiseTargetRms * velScale;

  initKSString(ksStrings[stringIndex], freq, kKsDecay, targetRms);
}

// 处理所有到了时间的 ScheduledPluck
void handleScheduledPlucks() {
  for (int i = 0; i < MAX_SCHEDULED_PLUCKS; ++i) {
    ScheduledPluck &sp = gPlucks[i];
    if (!sp.active) continue;
    if (gSampleCounter >= sp.triggerSample) {
      startPluck(sp.chordIndex, sp.stringIndex, sp.velocityNorm);
      sp.active = false;
    }
  }
}

// 安排一次扫弦：根据方向/和弦/力度，生成若干 ScheduledPluck
void scheduleStrum(StrumDirection dir, int chordIndex, int velocity) {
  if (chordIndex < 0 || chordIndex >= NUM_CHORDS) return;
  if (velocity < 0) velocity = 0;
  if (velocity > 127) velocity = 127;

  float vNorm = (float)velocity / 127.0f;
  // 避免完全 0，顺手做个稍微平滑一点的力度映射
  vNorm = 0.1f + 0.9f * vNorm;

  MidiChord &ch = chords[chordIndex];
  int stringsToUse = (int)min((size_t)NUM_STRINGS, ch.count);

  // 力度越大，弦间延时越短
  float interDelayMs = kInterDelayMsSlow
                       - vNorm * (kInterDelayMsSlow - kInterDelayMsFast);
  if (interDelayMs < kInterDelayMsFast) interDelayMs = kInterDelayMsFast;

  Serial.print("Strum: ");
  Serial.print((dir == STRUM_DOWN) ? "DOWN " : "UP   ");
  Serial.print("Chord=");
  Serial.print(chords[chordIndex].name);
  Serial.print("  v=");
  Serial.print(velocity);
  Serial.print("  interDelayMs=");
  Serial.println(interDelayMs);

  for (int localIdx = 0; localIdx < stringsToUse; ++localIdx) {
    // 下扫：从低音到高音；上扫：从高音到低音
    int stringIndex = (dir == STRUM_DOWN)
                        ? localIdx
                        : (stringsToUse - 1 - localIdx);

    // 对应的触发时间（相对当前 sampleCounter）
    float offsetMs = interDelayMs * localIdx;
    uint32_t offsetSamples = (uint32_t)(offsetMs * 0.001f * (float)kSampleRate);
    uint32_t trigSample = gSampleCounter + offsetSamples;

    // 找一个空闲 slot
    for (int i = 0; i < MAX_SCHEDULED_PLUCKS; ++i) {
      if (!gPlucks[i].active) {
        gPlucks[i].active = true;
        gPlucks[i].triggerSample = trigSample;
        gPlucks[i].chordIndex = chordIndex;
        gPlucks[i].stringIndex = stringIndex;
        gPlucks[i].velocityNorm = vNorm;
        break;
      }
    }
  }
}

// ---- Tone shaping + 混合 ----
float mixAndShapeOutput() {
  // 混合所有弦
  float out = 0.0f;
  int activeCount = 0;
  for (int i = 0; i < NUM_STRINGS; ++i) {
    if (ksStrings[i].active) {
      out += processKSString(ksStrings[i]);
      activeCount++;
    }
  }
  if (activeCount > 0) {
    out /= (float)activeCount;
  }

  // Tone shaping：存在感 + 箱体
  // 1) 高频/中频分离：低通 LP_tone 得到“整体”，out-LP_tone 是 presence
  toneState.lpTone += kLpToneAlpha * (out - toneState.lpTone);
  float presence = out - toneState.lpTone;
  float shaped = (1.0f - kPresenceMix) * out + kPresenceMix * presence;

  // 2) 箱体共鸣（低频缓慢响应）
  toneState.lpBody += kBodyAlpha * (shaped - toneState.lpBody);
  shaped = (1.0f - kBodyMix) * shaped + kBodyMix * toneState.lpBody;

  // 3) 总体增益
  shaped *= kOutputGain;

  // 简单限幅
  if (shaped > 1.0f) shaped = 1.0f;
  if (shaped < -1.0f) shaped = -1.0f;

  return shaped;
}

// ---- 每个 sample 的合成入口 ----
float renderSample() {
  // 先检查有没有到点的 pluck 需要启动
  handleScheduledPlucks();

  // 更新 sample 计数（作为时间轴）
  gSampleCounter++;

  // 输出混音 + tone shaping
  return mixAndShapeOutput();
}

// ================== 串口命令处理 ==================
void processCommand(const char *cmd) {
  // 格式： <D/U> <chordIndex> <velocity>
  char dirChar;
  int chordIndex, vel;

  if (sscanf(cmd, " %c %d %d", &dirChar, &chordIndex, &vel) == 3) {
    StrumDirection dir = (dirChar == 'U' || dirChar == 'u') ? STRUM_UP : STRUM_DOWN;
    if (chordIndex < 0) chordIndex = 0;
    if (chordIndex >= NUM_CHORDS) chordIndex = NUM_CHORDS - 1;
    if (vel < 0) vel = 0;
    if (vel > 127) vel = 127;

    scheduleStrum(dir, chordIndex, vel);
  } else {
    Serial.print("Parse error: ");
    Serial.println(cmd);
    Serial.println("Usage: D 4 100  (dir D/U, chord 0..5, vel 0..127)");
  }
}

void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (cmdLen > 0) {
        cmdBuf[cmdLen] = '\0';
        processCommand(cmdBuf);
        cmdLen = 0;
      }
    } else {
      if (cmdLen < CMD_BUF_SIZE - 1) {
        cmdBuf[cmdLen++] = c;
      }
    }
  }
}

// ================== Arduino 入口 ==================

void setup() {
  Serial.begin(115200);
  Serial1.begin(19200, SERIAL_8N1, 11, 10);
  delay(1000);
  // Serial.println();
  // Serial.println("ESP32-S2 + ESP_I2S  Guitar Engine");
  // Serial.println("Command format: D 4 110  (D/U, chordIndex 0..5, velocity 0..127)");
  // Serial.println("Chord mapping: 0=C,1=Dm,2=Em,3=F,4=G,5=Am");
  // Serial.println();

  randomSeed((uint32_t)millis());

  // 清空 KS & pluck 队列
  for (int i = 0; i < NUM_STRINGS; ++i) {
    ksStrings[i].active = false;
  }
  for (int i = 0; i < MAX_SCHEDULED_PLUCKS; ++i) {
    gPlucks[i].active = false;
  }
  toneState.lpTone = 0.0f;
  toneState.lpBody = 0.0f;
  gSampleCounter = 0;

  // 配置 I2S（模式参考 simple tone 示例）
  i2s_data_bit_width_t bps = I2S_DATA_BIT_WIDTH_16BIT;
  i2s_mode_t mode = I2S_MODE_STD;
  i2s_slot_mode_t slot = I2S_SLOT_MODE_STEREO;

  i2s.setPins(I2S_BCLK, I2S_LRC, I2S_DIN);

  if (!i2s.begin(mode, kSampleRate, bps, slot)) {
    Serial.println("Failed to initialize I2S!");
    while (1) { delay(1000); }
  }
}

void loop() {
  // // 先处理串口命令（可能会触发新的扫弦调度）
  handleSerial();

  //read form 328PB
  String chord, gesture;
  int vel = 80;
  if (getFromAtmega(chord, gesture, vel)) {
    Serial.print("Received Chord:   ");
    Serial.println(chord);
    Serial.print("Received Gesture: ");
    Serial.println(gesture);
    Serial.print("Received Velocity: ");
    Serial.println(vel);
    int chordIndex;
    // chord map (todo)
    if (chord.equalsIgnoreCase("C")) chordIndex = 0;
    else if (chord.equalsIgnoreCase("Dm")) chordIndex = 1;
    else if (chord.equalsIgnoreCase("Em")) chordIndex = 2;
    else if (chord.equalsIgnoreCase("F")) chordIndex = 3;
    else if (chord.equalsIgnoreCase("G")) chordIndex = 4;
    else if (chord.equalsIgnoreCase("Am")) chordIndex = 5;

    StrumDirection dir;
    if (gesture.equalsIgnoreCase("STRUM_UP")) {
        dir = STRUM_UP;
    }
    else if (gesture.equalsIgnoreCase("STRUM_DOWN")) {
        dir = STRUM_DOWN;
    }
    scheduleStrum(dir, chordIndex, vel);
  }


  // 每次 loop 输出一个 sample（左右声道相同）
  float s = renderSample();
  int16_t v = (int16_t)(s * 32760.0f);

  uint8_t lo = v & 0xFF;
  uint8_t hi = (v >> 8) & 0xFF;

  // Left
  i2s.write(lo);
  i2s.write(hi);
  // Right
  i2s.write(lo);
  i2s.write(hi);
}