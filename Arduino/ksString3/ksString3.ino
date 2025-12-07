#include <Arduino.h>
#include <ESP_I2S.h>
#include <math.h>

// ================== I2S 引脚 ==================
#define I2S_LRC   17
#define I2S_BCLK  18
#define I2S_DIN   14

// ================== 合成 & 音色参数（集中调节区） ==================
#define NUM_STRINGS        3          // 每个和弦用几根“虚拟弦”
#define MAX_KS_DELAY       512        // Karplus-Strong 最大延迟长度

// 基本音频
const int   kSampleRate        = 16000;   // 采样率，16k 稳定且足够
const float kPluckDurationSec  = 1.0f;    // 每次拨弦时长（秒）
const int   kPlucksPerChord    = 2;       // 每个和弦拨几次（2 = 刷刷）

// 包络（流行伴奏风格：起音快，尾巴中等）
const float kAttackSec         = 0.015f;  // Attack 时间（秒），10–20ms
const float kDecaySec          = 0.75f;   // Decay 时间（秒）

// Karplus-Strong 衰减（越接近 1 越持久）
const float kKsDecay           = 0.994f;  // 0.992–0.996 可微调

// 拨弦噪声 RMS 目标（决定初始能量 & 整体响度）
const float kNoiseTargetRms    = 0.23f;   // 0.20–0.27

// 每根弦的 detune（cent），让和弦更宽一点
const float kDetuneCents[NUM_STRINGS] = { -3.0f, 0.0f, 3.0f };

// Tone shaping：频响和箱体感
const float kLpToneAlpha       = 0.24f;   // 高频低通强度：0.2 偏暖，0.3 偏亮
const float kPresenceMix       = 0.28f;   // 存在感（拨弦清脆度）0~0.4
const float kBodyAlpha         = 0.02f;   // 箱体共鸣低频滤波速度
const float kBodyMix           = 0.25f;   // 箱体占比 0~0.4

// 总体增益
const float kOutputGain        = 1.3f;    // 1.1–1.4，根据喇叭和耳朵来调

// ================== Karplus-Strong 结构体 ==================
struct KSString {
  float buffer[MAX_KS_DELAY];
  int   length;
  int   index;
  float decay;
  bool  active;
};

KSString ksStrings[NUM_STRINGS];

// ================== 和声配置（C 调 4536251） ==================
struct MidiChord {
  const char*   name;
  const uint8_t* notes;
  size_t        count;
};

// C 大调常用和弦
const uint8_t CmajNotes[] = {60, 64, 67};  // C4 E4 G4
const uint8_t GmajNotes[] = {55, 59, 62};  // G3 B3 D4
const uint8_t AminNotes[] = {57, 60, 64};  // A3 C4 E4
const uint8_t EminNotes[] = {52, 55, 59};  // E3 G3 B3
const uint8_t FmajNotes[] = {53, 57, 60};  // F3 A3 C4
const uint8_t DminNotes[] = {50, 53, 57};  // D3 F3 A3

enum {
  CHORD_C  = 0,
  CHORD_Dm = 1,
  CHORD_Em = 2,
  CHORD_F  = 3,
  CHORD_G  = 4,
  CHORD_Am = 5,
};

MidiChord chords[] = {
  { "C",  CmajNotes,  sizeof(CmajNotes)  / sizeof(uint8_t) },  // 0
  { "Dm", DminNotes,  sizeof(DminNotes)  / sizeof(uint8_t) },  // 1
  { "Em", EminNotes,  sizeof(EminNotes)  / sizeof(uint8_t) },  // 2
  { "F",  FmajNotes,  sizeof(FmajNotes)  / sizeof(uint8_t) },  // 3
  { "G",  GmajNotes,  sizeof(GmajNotes)  / sizeof(uint8_t) },  // 4
  { "Am", AminNotes,  sizeof(AminNotes)  / sizeof(uint8_t) },  // 5
};

const int NUM_CHORDS = sizeof(chords) / sizeof(chords[0]);

// 4536251：F G Em Am Dm G C
const int kProgression[] = {
  CHORD_F,   // 4
  CHORD_G,   // 5
  CHORD_Em,  // 3
  CHORD_Am,  // 6
  CHORD_Dm,  // 2
  CHORD_G,   // 5
  CHORD_C    // 1
};
const int kProgressionLen = sizeof(kProgression) / sizeof(kProgression[0]);

// ================== 合成状态 ==================
struct SynthState {
  int      progressionPos;     // 当前进行 index
  int      currentChordIndex;  // 当前和弦
  uint32_t samplesInPluck;     // 当前 pluck 已经跑了多少采样
  uint32_t samplesPerPluck;    // 每次 pluck 的采样数
  int      pluckCountForChord; // 当前和弦已经拨了几次
};

SynthState synth;

// Tone shaping 状态（滤波器记忆）
struct ToneState {
  float lpTone;
  float lpBody;
};

// ⚠️ 这里改名，避免和 Arduino 的 tone() 冲突
ToneState toneState;

// ================== I2S 实例 ==================
I2SClass i2s;

// ================== 工具函数 ==================
float midiToFreq(uint8_t midi) {
  return 440.0f * powf(2.0f, ((int)midi - 69) / 12.0f);
}

// ---- 初始化一根 KS 弦：平滑噪声 + RMS 归一化 ----
void initKSString(KSString &s, float freq, float decay) {
  if (freq <= 0.0f) {
    s.active = false;
    return;
  }
  int len = (int)((float)kSampleRate / freq + 0.5f);
  if (len < 2) len = 2;
  if (len > MAX_KS_DELAY) len = MAX_KS_DELAY;

  s.length = len;
  s.index  = 0;
  s.decay  = decay;
  s.active = true;

  float prev  = 0.0f;
  float sumSq = 0.0f;

  for (int i = 0; i < len; ++i) {
    int   r   = random(-32768, 32767);
    float val = (float)r / 32768.0f;     // -1 ~ 1

    // 平滑一点，让起音更木、少毛刺
    val = 0.4f * val + 0.6f * prev;
    prev = val;

    s.buffer[i] = val;
    sumSq += val * val;
  }

  for (int i = len; i < MAX_KS_DELAY; ++i) {
    s.buffer[i] = 0.0f;
  }

  // RMS 归一化，让每次 pluck 能量一致
  if (sumSq > 1e-6f) {
    float rms = sqrtf(sumSq / (float)len);
    float scale = kNoiseTargetRms / rms;
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

// ---- 初始化当前和弦的所有弦 ----
void initChordStrings(int chordIndex) {
  if (chordIndex < 0 || chordIndex >= NUM_CHORDS) return;

  MidiChord &ch = chords[chordIndex];
  int stringsToUse = (int)min((size_t)NUM_STRINGS, ch.count);

  for (int i = 0; i < stringsToUse; ++i) {
    float freq = midiToFreq(ch.notes[i]);

    // 轻微 detune，拉宽一点
    float detune_cents = kDetuneCents[i];
    float detune = powf(2.0f, detune_cents / 1200.0f);
    freq *= detune;

    initKSString(ksStrings[i], freq, kKsDecay);
  }
  for (int i = stringsToUse; i < NUM_STRINGS; ++i) {
    ksStrings[i].active = false;
  }
}

void triggerPluckForChord(int chordIndex, bool printName) {
  synth.currentChordIndex  = chordIndex;
  synth.samplesInPluck     = 0;
  initChordStrings(chordIndex);

  if (printName) {
    Serial.print("Chord: ");
    Serial.print(chords[chordIndex].name);
    Serial.print("  pluck ");
    Serial.println(synth.pluckCountForChord + 1);
  }
}

// ---- 更新和弦/拨弦状态（进度管理）----
void updateChordAndPluck() {
  if (synth.samplesInPluck >= synth.samplesPerPluck) {
    synth.samplesInPluck = 0;
    synth.pluckCountForChord++;

    if (synth.pluckCountForChord < kPlucksPerChord) {
      // 同一和弦再拨一次
      triggerPluckForChord(synth.currentChordIndex, true);
    } else {
      // 换下一个和弦
      synth.pluckCountForChord = 0;
      synth.progressionPos = (synth.progressionPos + 1) % kProgressionLen;
      int nextChord = kProgression[synth.progressionPos];
      triggerPluckForChord(nextChord, true);
    }
  }
}

// ---- 合成当前采样：KS 和弦 + 包络 + EQ/Tone ----
float renderSample() {
  // 先更新 pluck 状态
  updateChordAndPluck();
  synth.samplesInPluck++;

  // 1) Karplus-Strong 混合
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

  // 2) 包络：Attack + Decay
  float t = (float)synth.samplesInPluck / (float)kSampleRate;
  float env;
  if (t < kAttackSec) {
    env = t / kAttackSec;
  } else if (t < kPluckDurationSec) {
    float dt = t - kAttackSec;
    env = expf(-dt / kDecaySec);
  } else {
    env = 0.0f;
  }
  out *= env;

  // 3) Tone shaping：存在感 + 箱体

  // 3.1 高频/中频分离：低通 LP_tone 得到“整体”，out-LP_tone 是 presence
  toneState.lpTone += kLpToneAlpha * (out - toneState.lpTone);
  float presence = out - toneState.lpTone;
  float shaped = (1.0f - kPresenceMix) * out + kPresenceMix * presence;

  // 3.2 箱体共鸣（低频缓慢响应）
  toneState.lpBody += kBodyAlpha * (shaped - toneState.lpBody);
  shaped = (1.0f - kBodyMix) * shaped + kBodyMix * toneState.lpBody;

  // 4) 总体增益
  shaped *= kOutputGain;

  // 简单限幅避免极端溢出
  if (shaped > 1.0f)  shaped = 1.0f;
  if (shaped < -1.0f) shaped = -1.0f;

  return shaped;
}

// ================== Arduino 入口 ==================

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32-S2 + ESP_I2S  Guitar 4536251 (Pop backing, tunable)");

  randomSeed((uint32_t)millis());

  // 初始化合成状态
  synth.progressionPos     = 0;
  synth.currentChordIndex  = kProgression[0];
  synth.samplesInPluck     = 0;
  synth.samplesPerPluck    = (uint32_t)(kSampleRate * kPluckDurationSec);
  synth.pluckCountForChord = 0;

  toneState.lpTone = 0.0f;
  toneState.lpBody = 0.0f;

  // 配置 I2S（跟官方 example 一样的模式，只换了采样率）
  i2s_data_bit_width_t bps  = I2S_DATA_BIT_WIDTH_16BIT;
  i2s_mode_t           mode = I2S_MODE_STD;
  i2s_slot_mode_t      slot = I2S_SLOT_MODE_STEREO;

  i2s.setPins(I2S_BCLK, I2S_LRC, I2S_DIN);

  if (!i2s.begin(mode, kSampleRate, bps, slot)) {
    Serial.println("Failed to initialize I2S!");
    while (1) { delay(1000); }
  }

  // 从第一个和弦开始拨第一下
  triggerPluckForChord(synth.currentChordIndex, true);
}

void loop() {
  // 像官方示例一样，一次写一个 sample（左右声道相同）
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
