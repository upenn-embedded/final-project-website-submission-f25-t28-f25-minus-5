// esp32_guitar_engine.ino
//
// Karplus–Strong 六弦 + AutoKey（晴天）+ 切音“啪” + 主音量控制。
// 输入：ATmega UART（实战） / USB Serial（调试）
// 输出：I2S → 扬声器

#include <Arduino.h>
#include <ESP_I2S.h>
#include <math.h>
#include "esp32_uart.h"       // ATmega UART 协议解析（带 volume 0..127）
#include "guitar_params.h"    // 所有可调参数

// ============ I2S 硬件引脚（根据实际连线调整） ============
#define I2S_LRC  17
#define I2S_BCLK 18
#define I2S_DIN  14

// ============================================================
// 1. 类型定义 & 全局结构
// ============================================================

struct KSString {
  float buffer[kMaxKsDelay];
  int   length;
  int   index;
  float decay;
  bool  active;
};

struct MidiChord {
  const char   *name;    // "C", "Am", "G7", "Dsus4" ...
  const uint8_t *notes;  // 3 个 MIDI note
  size_t        count;
};

enum StrumDirection {
  STRUM_DOWN = 0,  // 低音 → 高音
  STRUM_UP   = 1   // 高音 → 低音
};

struct ScheduledPluck {
  bool     active;
  uint32_t triggerSample;
  int      chordIndex;
  int      stringIndex;   // 0..kNumStrings-1
  float    velocityNorm;  // 0~1
};

struct ToneState {
  float lpTone;
  float lpBody;
};

struct AutoKeyState {
  int seqIndex;     // 0..kAutoKeySeqLength-1
  int repeatCount;  // 0..kAutoKeyRepeatsPerChord-1
};

enum InputMode {
  INPUT_MODE_ATMEGA = 0,
  INPUT_MODE_SERIAL = 1
};

// 切音噪声状态
struct ChokeState {
  bool  active;
  int   remainingSamples;
  float env;
};

// ============================================================
// 2. 和弦库定义 & 名字 ↔ 索引映射
// ============================================================

// ----- Major triads -----
const uint8_t CmajNotes[] = { 60, 64, 67 }; // C4 E4 G4
const uint8_t GmajNotes[] = { 55, 59, 62 }; // G3 B3 D4
const uint8_t DmajNotes[] = { 62, 66, 69 }; // D4 F#4 A4
const uint8_t AmajNotes[] = { 57, 61, 64 }; // A3 C#4 E4
const uint8_t EmajNotes[] = { 52, 56, 59 }; // E3 G#3 B3
const uint8_t FmajNotes[] = { 53, 57, 60 }; // F3 A3 C4

// ----- Minor triads -----
const uint8_t AminNotes[]   = { 57, 60, 64 }; // A3 C4 E4
const uint8_t EminNotes[]   = { 52, 55, 59 }; // E3 G3 B3
const uint8_t DminNotes[]   = { 50, 53, 57 }; // D3 F3 A3
const uint8_t BminNotes[]   = { 59, 62, 66 }; // B3 D4 F#4
const uint8_t FsminNotes[]  = { 54, 57, 61 }; // F#3 A3 C#4
const uint8_t GminNotes[]   = { 55, 58, 62 }; // G3 Bb3 D4

// ----- Dominant 7ths（R, 3rd, b7）-----
const uint8_t C7Notes[] = { 48, 52, 58 }; // C3 E3 Bb3
const uint8_t G7Notes[] = { 55, 59, 65 }; // G3 B3 F4
const uint8_t D7Notes[] = { 50, 54, 60 }; // D3 F#3 C4
const uint8_t A7Notes[] = { 57, 61, 67 }; // A3 C#4 G4
const uint8_t E7Notes[] = { 52, 56, 62 }; // E3 G#3 D4
const uint8_t B7Notes[] = { 59, 63, 69 }; // B3 D#4 A4

// ----- Sus & dim -----
const uint8_t Dsus4Notes[] = { 50, 55, 57 }; // D3 G3 A3
const uint8_t Gsus4Notes[] = { 55, 60, 62 }; // G3 C4 D4
const uint8_t Asus4Notes[] = { 57, 62, 64 }; // A3 D4 E4
const uint8_t Esus4Notes[] = { 52, 57, 59 }; // E3 A3 B3
const uint8_t BdimNotes[]  = { 59, 62, 65 }; // B3 D4 F4
const uint8_t FsDimNotes[] = { 54, 57, 60 }; // F#3 A3 C4

// ----- Cmaj7（新增）-----
// 用 C4 E4 B4（root、3rd、maj7），没有 5th 问题不大
const uint8_t Cmaj7Notes[] = { 60, 64, 71 }; // C4 E4 B4

// chords[] 顺序要与 guitar_params.h 中的注释保持一致：
//  0:C,  1:G,  2:D,   3:A,   4:E,   5:F,
//  6:C7, 7:G7, 8:D7,  9:A7, 10:E7, 11:B7,
//  12:Am,13:Em,14:Dm,15:Bm,16:F#m,17:Gm,
//  18:Dsus4,19:Gsus4,20:Asus4,21:Esus4,22:Bdim,23:F#dim,
//  24:Cmaj7
MidiChord chords[] = {
  // Major triads 0..5
  { "C",     CmajNotes,    sizeof(CmajNotes)    / sizeof(uint8_t) }, // 0
  { "G",     GmajNotes,    sizeof(GmajNotes)    / sizeof(uint8_t) }, // 1
  { "D",     DmajNotes,    sizeof(DmajNotes)    / sizeof(uint8_t) }, // 2
  { "A",     AmajNotes,    sizeof(AmajNotes)    / sizeof(uint8_t) }, // 3
  { "E",     EmajNotes,    sizeof(EmajNotes)    / sizeof(uint8_t) }, // 4
  { "F",     FmajNotes,    sizeof(FmajNotes)    / sizeof(uint8_t) }, // 5

  // Dominant 7ths 6..11
  { "C7",    C7Notes,      sizeof(C7Notes)      / sizeof(uint8_t) }, // 6
  { "G7",    G7Notes,      sizeof(G7Notes)      / sizeof(uint8_t) }, // 7
  { "D7",    D7Notes,      sizeof(D7Notes)      / sizeof(uint8_t) }, // 8
  { "A7",    A7Notes,      sizeof(A7Notes)      / sizeof(uint8_t) }, // 9
  { "E7",    E7Notes,      sizeof(E7Notes)      / sizeof(uint8_t) }, // 10
  { "B7",    B7Notes,      sizeof(B7Notes)      / sizeof(uint8_t) }, // 11

  // Minor triads 12..17
  { "Am",    AminNotes,    sizeof(AminNotes)    / sizeof(uint8_t) }, // 12
  { "Em",    EminNotes,    sizeof(EminNotes)    / sizeof(uint8_t) }, // 13
  { "Dm",    DminNotes,    sizeof(DminNotes)    / sizeof(uint8_t) }, // 14
  { "Bm",    BminNotes,    sizeof(BminNotes)    / sizeof(uint8_t) }, // 15
  { "F#m",   FsminNotes,   sizeof(FsminNotes)   / sizeof(uint8_t) }, // 16
  { "Gm",    GminNotes,    sizeof(GminNotes)    / sizeof(uint8_t) }, // 17

  // Sus & dim 18..23
  { "Dsus4", Dsus4Notes,   sizeof(Dsus4Notes)   / sizeof(uint8_t) }, // 18
  { "Gsus4", Gsus4Notes,   sizeof(Gsus4Notes)   / sizeof(uint8_t) }, // 19
  { "Asus4", Asus4Notes,   sizeof(Asus4Notes)   / sizeof(uint8_t) }, // 20
  { "Esus4", Esus4Notes,   sizeof(Esus4Notes)   / sizeof(uint8_t) }, // 21
  { "Bdim",  BdimNotes,    sizeof(BdimNotes)    / sizeof(uint8_t) }, // 22
  { "F#dim", FsDimNotes,   sizeof(FsDimNotes)   / sizeof(uint8_t) }, // 23

  // 24：新增 Cmaj7
  { "Cmaj7", Cmaj7Notes,   sizeof(Cmaj7Notes)   / sizeof(uint8_t) }  // 24
};

const int NUM_CHORDS = sizeof(chords) / sizeof(chords[0]);

int chordNameToIndex(const String &name) {
  for (int i = 0; i < NUM_CHORDS; ++i) {
    if (name.equalsIgnoreCase(chords[i].name)) {
      return i;
    }
  }
  return -1;
}

// ============================================================
// 3. 全局状态 & I2S 实例
// ============================================================

KSString       gStrings[kNumStrings];
ScheduledPluck gPlucks[kMaxScheduledPlucks];
ToneState      gToneState = {0.0f, 0.0f};
AutoKeyState   gAutoKey   = {0, 0};
ChokeState     gChoke     = {false, 0, 0.0f};
uint32_t       gSampleCounter = 0;
InputMode      gInputMode     = INPUT_MODE_ATMEGA;

// 主音量控制（0.0~1.0），由 ATmega 的 volume(0..127) 或 Serial vol 命令设置
float          gMasterVolume  = 1.0f;

I2SClass i2s;

// ============================================================
// 4. 工具函数：MIDI ↔ 频率、AutoKey 状态管理、切音
// ============================================================

float midiToFreq(uint8_t midi) {
  return 440.0f * powf(2.0f, ((int)midi - 69) / 12.0f);
}

void autoKeyReset() {
  gAutoKey.seqIndex    = 0;
  gAutoKey.repeatCount = 0;
}

int autoKeyNextChordIndex() {
  int chordIndex = kAutoKeySeq[gAutoKey.seqIndex];

  gAutoKey.repeatCount++;
  if (gAutoKey.repeatCount >= kAutoKeyRepeatsPerChord) {
    gAutoKey.repeatCount = 0;
    gAutoKey.seqIndex++;
    if (gAutoKey.seqIndex >= kAutoKeySeqLength) {
      gAutoKey.seqIndex = 0;
    }
  }
  return chordIndex;
}

// 切音：立即停掉所有弦，并开启短噪声“啪”
void triggerChoke() {
  // 停掉所有弦的声音
  for (int i = 0; i < kNumStrings; ++i) {
    gStrings[i].active = false;
  }

  // 开启一个短噪声包络
  gChoke.active           = true;
  gChoke.remainingSamples = kChokeLengthSamples;
  gChoke.env              = 1.0f;

  Serial.println("[Choke] CUT + smack");
}

// ============================================================
// 5. Karplus–Strong 引擎：初始化 & 每 sample 更新
// ============================================================

void initKSString(KSString &s, float freq, float decay, float targetRms) {
  if (freq <= 0.0f) {
    s.active = false;
    return;
  }
  int len = (int)((float)kSampleRate / freq + 0.5f);
  if (len < 2)           len = 2;
  if (len > kMaxKsDelay) len = kMaxKsDelay;

  s.length = len;
  s.index  = 0;
  s.decay  = decay;
  s.active = true;

  float prev  = 0.0f;
  float sumSq = 0.0f;

  for (int i = 0; i < len; ++i) {
    int r      = random(-32768, 32767);
    float val  = (float)r / 32768.0f;

    // 稍微做一点低通，避免太“沙”
    val  = 0.4f * val + 0.6f * prev;
    prev = val;

    s.buffer[i] = val;
    sumSq      += val * val;
  }

  // 剩余 buffer 清零
  for (int i = len; i < kMaxKsDelay; ++i) {
    s.buffer[i] = 0.0f;
  }

  // 归一化 RMS
  if (sumSq > 1e-6f) {
    float rms   = sqrtf(sumSq / (float)len);
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
  s.index      = i1;

  return y;
}

// ============================================================
// 6. 拨弦调度：六弦铺和弦 + 按时间触发 pluck + 扫弦封装
// ============================================================

void startPluck(int chordIndex, int stringIndex, float velocityNorm) {
  if (chordIndex < 0 || chordIndex >= NUM_CHORDS) return;
  if (stringIndex < 0 || stringIndex >= kNumStrings) return;

  MidiChord &ch = chords[chordIndex];
  if (ch.count < 3) return;

  uint8_t root  = ch.notes[0];
  uint8_t third = ch.notes[1];
  uint8_t fifth = ch.notes[2];

  uint8_t noteMidi;
  switch (stringIndex) {
    case 0: noteMidi = root  - 12; break;
    case 1: noteMidi = fifth - 12; break;
    case 2: noteMidi = root;       break;
    case 3: noteMidi = third;      break;
    case 4: noteMidi = fifth;      break;
    default: noteMidi = root + 12; break;
  }

  float freq = midiToFreq(noteMidi);

  float detune_cents = kDetuneCents[stringIndex];
  float detune       = powf(2.0f, detune_cents / 1200.0f);
  freq *= detune;

  float v        = constrain(velocityNorm, 0.0f, 1.0f);
  float velScale = kVelRmsScaleMin + (kVelRmsScaleMax - kVelRmsScaleMin) * v;
  float targetRms = kBaseNoiseTargetRms * velScale;
  float decay     = kKsDecayMin + (kKsDecayMax - kKsDecayMin) * v;

  initKSString(gStrings[stringIndex], freq, decay, targetRms);
}

void handleScheduledPlucks() {
  for (int i = 0; i < kMaxScheduledPlucks; ++i) {
    ScheduledPluck &sp = gPlucks[i];
    if (!sp.active) continue;
    if (gSampleCounter >= sp.triggerSample) {
      startPluck(sp.chordIndex, sp.stringIndex, sp.velocityNorm);
      sp.active = false;
    }
  }
}

void scheduleStrum(StrumDirection dir, int chordIndex, int velocity) {
  if (chordIndex < 0 || chordIndex >= NUM_CHORDS) return;
  if (velocity < 0)   velocity = 0;
  if (velocity > 127) velocity = 127;

  float vNorm = (float)velocity / 127.0f;
  vNorm = 0.1f + 0.9f * vNorm;  // 保证最弱扫也有一点能量

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

  for (int localIdx = 0; localIdx < kNumStrings; ++localIdx) {
    int stringIndex = (dir == STRUM_DOWN)
                        ? localIdx
                        : (kNumStrings - 1 - localIdx);

    float offsetMs       = interDelayMs * localIdx;
    uint32_t offsetSamples = (uint32_t)(offsetMs * 0.001f * (float)kSampleRate);
    uint32_t trigSample    = gSampleCounter + offsetSamples;

    for (int i = 0; i < kMaxScheduledPlucks; ++i) {
      if (!gPlucks[i].active) {
        gPlucks[i].active        = true;
        gPlucks[i].triggerSample = trigSample;
        gPlucks[i].chordIndex    = chordIndex;
        gPlucks[i].stringIndex   = stringIndex;
        gPlucks[i].velocityNorm  = vNorm;
        break;
      }
    }
  }
}

// ============================================================
// 7. 输入处理：串口命令 / ATmega（含 AUTOKEY + 切音 + 主音量）
// ============================================================

static const int CMD_BUF_SIZE = 64;
char cmdBuf[CMD_BUF_SIZE];
int  cmdLen = 0;

void setInputMode(InputMode mode) {
  if (gInputMode == mode) return;
  gInputMode = mode;
  Serial.print("Mode: ");
  Serial.println((mode == INPUT_MODE_SERIAL) ? "Serial" : "ATmega");
}

void processCommand(const char *cmd) {
  String line(cmd);
  line.trim();
  if (line.length() == 0) return;

  String low = line;
  low.toLowerCase();

  // ---------- 1) 模式切换 ----------
  if (low.startsWith("mode")) {
    low.remove(0, 4);
    low.trim();
    if (low == "serial") {
      setInputMode(INPUT_MODE_SERIAL);
    } else if (low == "atmega" || low == "hw") {
      setInputMode(INPUT_MODE_ATMEGA);
    } else {
      Serial.print("Unknown mode: ");
      Serial.println(line);
      Serial.println("Valid: mode serial | mode atmega");
    }
    return;
  }

  // ---------- 1.5) 主音量设置：vol / volume ----------
  //
  // 例： vol 80 / volume 50   （按百分比）
  //
  if (low.startsWith("vol")) {
    int spacePos = low.indexOf(' ');
    if (spacePos < 0) {
      Serial.println("Usage: vol 0..100");
      return;
    }
    String numStr = low.substring(spacePos + 1);
    numStr.trim();
    if (numStr.length() == 0) {
      Serial.println("Usage: vol 0..100");
      return;
    }
    int volVal = numStr.toInt();
    if (volVal < 0)   volVal = 0;
    if (volVal > 100) volVal = 100;
    gMasterVolume = volVal / 100.0f;
    Serial.print("Master volume set to ");
    Serial.print(volVal);
    Serial.println("%");
    return;
  }

  // ---------- 2) Serial 切音：行首是 m / M ----------
  //
  // 语法示例：
  //   m
  //   m 0 100
  //   m ak 127
  // 都视为 “切音 + 啪”，不扫新和弦，只停弦 + 噪声。
  //
  const char *p = line.c_str();
  while (*p == ' ' || *p == '\t') ++p;
  if (*p == 'm' || *p == 'M') {
    triggerChoke();
    return;
  }

  // ---------- 3) Serial AutoKey： <dir> ak <velocity> ----------
  //
  // 语法：u ak 127 / d ak 90
  // dir: u = up, d = down
  //
  char dirCharAk;
  char word[16];
  int  velAk;
  if (sscanf(line.c_str(), " %c %15s %d", &dirCharAk, word, &velAk) == 3) {
    String w(word);
    if (w.equalsIgnoreCase("ak") || w.equalsIgnoreCase("autokey")) {
      dirCharAk = tolower(dirCharAk);
      StrumDirection dir =
          (dirCharAk == 'u') ? STRUM_UP : STRUM_DOWN;

      if (velAk < 0)   velAk = 0;
      if (velAk > 127) velAk = 127;

      int chordIndex = autoKeyNextChordIndex();
      scheduleStrum(dir, chordIndex, velAk);
      return;
    }
  }

  // ---------- 4) 普通 Serial 手动扫弦： <dir> <chordIndex> <velocity> ----------
  //
  // 语法：<dir> <index> <vel>
  // 例： d 0 100  → C
  //
  char dirChar;
  int  chordIndex, vel;
  if (sscanf(line.c_str(), " %c %d %d", &dirChar, &chordIndex, &vel) == 3) {
    dirChar = tolower(dirChar);
    StrumDirection dir =
        (dirChar == 'u') ? STRUM_UP : STRUM_DOWN;

    if (chordIndex < 0)           chordIndex = 0;
    if (chordIndex >= NUM_CHORDS) chordIndex = NUM_CHORDS - 1;
    if (vel < 0)                  vel = 0;
    if (vel > 127)                vel = 127;

    autoKeyReset();  // 手动和弦视为“打断 AutoKey”
    scheduleStrum(dir, chordIndex, vel);
  } else {
    Serial.print("Parse error: ");
    Serial.println(line);
    Serial.print("Usage:\n  d 4 100      (dir d/u, chordIndex 0..");
    Serial.print(NUM_CHORDS - 1);
    Serial.println(", velocity 0..127)");
    Serial.println("  m            (cut current sound, short smack)");
    Serial.println("  u ak 127     (AutoKey UP   with velocity 127)");
    Serial.println("  d ak 90      (AutoKey DOWN with velocity 90)");
    Serial.println("  vol 80       (set master volume to 80%)");
    Serial.println("  mode serial  (switch to serial debug input)");
    Serial.println("  mode atmega  (use ATmega UART chord input)");
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

// ATmega 输入（含 AUTOKEY + 切音 + 主音量）
void handleAtmegaInput() {
  String chord, gesture;
  int vel    = 80;   // 0..127
  int volume = 127;  // 0..127

  if (!getFromAtmega(chord, gesture, vel, volume)) {
    return;  // 还没收到完整一帧
  }

  // clamp 一下，防错
  if (vel < 0)      vel = 0;
  if (vel > 127)    vel = 127;
  if (volume < 0)   volume = 0;
  if (volume > 127) volume = 127;

  // 更新主音量（映射到 0.0~1.0）
  gMasterVolume = volume / 127.0f;

  Serial.print("Received Chord:   ");  Serial.println(chord);
  Serial.print("Received Gesture: ");  Serial.println(gesture);
  Serial.print("Received Velocity:");  Serial.println(vel);
  Serial.print("Received Volume: ");   Serial.println(volume);

  String g = gesture;
  g.toUpperCase();

  // --- 切音手势：MUTE / CHOKE / CUT 之类 ---
  if (g.indexOf("MUTE") >= 0 || g.indexOf("CHOKE") >= 0 || g.indexOf("CUT") >= 0) {
    triggerChoke();
    return;
  }

  // --- 普通 up/down 扫弦 ---
  StrumDirection dir;
  if (g.indexOf("UP") >= 0) {
    dir = STRUM_UP;
  } else {
    dir = STRUM_DOWN;  // 默认 down
  }

  // AUTOKEY 模式：Chord = "AUTOKEY"
  if (chord.equalsIgnoreCase("AUTOKEY")) {
    int chordIndex = autoKeyNextChordIndex();
    scheduleStrum(dir, chordIndex, vel);
    return;
  }

  // 其它普通和弦：重置 AutoKey 状态
  autoKeyReset();

  int chordIndex = chordNameToIndex(chord);
  if (chordIndex < 0) {
    Serial.println("Unknown chord name from ATmega, ignoring.");
    return;
  }

  scheduleStrum(dir, chordIndex, vel);
}

// ============================================================
// 8. 音频渲染 & Arduino 入口
// ============================================================

float mixAndShapeOutput() {
  float out = 0.0f;
  int activeCount = 0;

  // 1) 所有弦的混音
  for (int i = 0; i < kNumStrings; ++i) {
    if (gStrings[i].active) {
      out += processKSString(gStrings[i]);
      activeCount++;
    }
  }
  if (activeCount > 0) {
    out /= (float)activeCount;
  }

  // 2) 切音噪声 “啪”
  if (gChoke.active) {
    int r = random(-32768, 32767);
    float n = (float)r / 32768.0f;
    n *= (kChokeBaseAmp * gChoke.env);

    out += n;

    gChoke.env *= kChokeEnvDecay;
    gChoke.remainingSamples--;
    if (gChoke.remainingSamples <= 0 || fabsf(gChoke.env) < 1e-3f) {
      gChoke.active = false;
    }
  }

  // 3) Tone shaping
  gToneState.lpTone += kLpToneAlpha * (out - gToneState.lpTone);
  float presence = out - gToneState.lpTone;
  float shaped   = (1.0f - kPresenceMix) * out + kPresenceMix * presence;

  gToneState.lpBody += kBodyAlpha * (shaped - gToneState.lpBody);
  shaped = (1.0f - kBodyMix) * shaped + kBodyMix * gToneState.lpBody;

  // 4) 总增益 = 固定音色增益 * 主音量（0~1）
  shaped *= (kOutputGain * gMasterVolume);

  if (shaped > 1.0f)  shaped = 1.0f;
  if (shaped < -1.0f) shaped = -1.0f;

  return shaped;
}

float renderSample() {
  handleScheduledPlucks();
  gSampleCounter++;
  return mixAndShapeOutput();
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(19200, SERIAL_8N1, 11, 10); // RX=11, TX=10（按你实际连线）

  delay(1000);
  randomSeed((uint32_t)millis());

  for (int i = 0; i < kNumStrings; ++i) {
    gStrings[i].active = false;
  }
  for (int i = 0; i < kMaxScheduledPlucks; ++i) {
    gPlucks[i].active = false;
  }
  gToneState.lpTone = 0.0f;
  gToneState.lpBody = 0.0f;
  gSampleCounter    = 0;
  autoKeyReset();
  gChoke.active     = false;
  gMasterVolume     = 1.0f;   // 默认 100%

  setInputMode(INPUT_MODE_ATMEGA);  // 默认用 ATmega

  i2s_data_bit_width_t bps  = I2S_DATA_BIT_WIDTH_16BIT;
  i2s_mode_t           mode = I2S_MODE_STD;
  i2s_slot_mode_t      slot = I2S_SLOT_MODE_STEREO;

  i2s.setPins(I2S_BCLK, I2S_LRC, I2S_DIN);
  if (!i2s.begin(mode, kSampleRate, bps, slot)) {
    Serial.println("Failed to initialize I2S!");
    while (1) { delay(1000); }
  }

  Serial.println("ESP32 Guitar Engine Ready.");
  Serial.println("Commands:");
  Serial.println("  mode serial  - Serial debug input");
  Serial.println("  mode atmega  - Use ATmega UART chord input");
  Serial.println("  d 0 100      - normal DOWN C");
  Serial.println("  u 12 90      - normal UP Am");
  Serial.println("  m            - choke: smack + stop all strings");
  Serial.println("  u ak 127     - AutoKey UP");
  Serial.println("  d ak 90      - AutoKey DOWN");
  Serial.println("  vol 80       - set master volume to 80%");
}

void loop() {
  handleSerial();

  if (gInputMode == INPUT_MODE_ATMEGA) {
    handleAtmegaInput();
  }

  float   s = renderSample();
  int16_t v = (int16_t)(s * 32760.0f);

  uint8_t lo = v & 0xFF;
  uint8_t hi = (v >> 8) & 0xFF;

  i2s.write(lo); i2s.write(hi); // Left
  i2s.write(lo); i2s.write(hi); // Right
}
