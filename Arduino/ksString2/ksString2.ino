#include <Arduino.h>
#include "driver/i2s.h"
#include <math.h>

// ================== I2S 参数 ==================
#define I2S_PORT   I2S_NUM_0

// 根据你的实际接线设置
#define I2S_BCLK   18   // BCLK  -> MAX98357A BCLK
#define I2S_LRCLK  17   // LRCLK -> MAX98357A LRC
#define I2S_DOUT   14   // DATA  -> MAX98357A DIN

#define SAMPLE_RATE        22050          // 22.05 kHz
#define FRAMES_PER_BUFFER  256            // 每次写 256 帧
#define NUM_STRINGS        3              // 每个和弦 3 根“弦”
#define MAX_KS_DELAY       512            // Karplus-Strong 最大延迟长度
#define CHORD_DURATION_SEC 0.9f           // 每个和弦大概 0.9 秒，更接近流行乐节奏

// ================== Karplus-Strong 拨弦模型 ==================
struct KSString {
  float buffer[MAX_KS_DELAY];
  int   length;       // 使用的延迟长度
  int   index;        // 当前写指针
  float decay;        // 衰减系数（0.99 ~ 1.0）
  bool  active;
};

KSString ksStrings[NUM_STRINGS];

// ========== 和弦定义（MIDI 音高）==========
struct MidiChord {
  const char*   name;
  const uint8_t* notes;
  size_t        count;
};

// 之前的和弦
const uint8_t CmajNotes[] = {60, 64, 67};  // C4 E4 G4
const uint8_t GmajNotes[] = {55, 59, 62};  // G3 B3 D4
const uint8_t AminNotes[] = {57, 60, 64};  // A3 C4 E4
const uint8_t EminNotes[] = {52, 55, 59};  // E3 G3 B3

// 新增：F 大调 & D 小调（都保持在差不多音区）
const uint8_t FmajNotes[] = {53, 57, 60};  // F3 A3 C4
const uint8_t DminNotes[] = {50, 53, 57};  // D3 F3 A3

// 为了好记，按 C, Dm, Em, F, G, Am 的顺序排
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

// ====== 4536251 万能和弦进行（C 调：F G Em Am Dm G C）======
const int progression[] = {
  CHORD_F,   // 4
  CHORD_G,   // 5
  CHORD_Em,  // 3
  CHORD_Am,  // 6
  CHORD_Dm,  // 2
  CHORD_G,   // 5
  CHORD_C    // 1
};
const int PROG_LEN = sizeof(progression) / sizeof(progression[0]);

int      progPos           = 0;                 // 当前进行位置
int      currentChordIndex = progression[0];    // 当前和弦索引
uint32_t samplesInChord    = 0;
uint32_t samplesPerChord   = (uint32_t)(SAMPLE_RATE * CHORD_DURATION_SEC);

// ================== 工具函数 ==================

float midiToFreq(uint8_t midi) {
  // 标准 440Hz：MIDI 69 = A4
  return 440.0f * powf(2.0f, ((int)midi - 69) / 12.0f);
}

// 初始化一根 Karplus-Strong 弦
void initKSString(KSString &s, float freq, float decay) {
  if (freq <= 0.0f) {
    s.active = false;
    return;
  }
  int len = (int)((float)SAMPLE_RATE / freq + 0.5f);
  if (len < 2) len = 2;
  if (len > MAX_KS_DELAY) len = MAX_KS_DELAY;

  s.length = len;
  s.index  = 0;
  s.decay  = decay;
  s.active = true;

  // 用随机噪声初始化缓冲（拨弦瞬间）
  for (int i = 0; i < len; ++i) {
    int r = random(-32768, 32767);
    s.buffer[i] = (float)r / 32768.0f;   // -1.0 ~ 1.0
  }
  // 多余部分清零
  for (int i = len; i < MAX_KS_DELAY; ++i) {
    s.buffer[i] = 0.0f;
  }
}

// 运行一拍 Karplus-Strong
float processKSString(KSString &s) {
  if (!s.active) return 0.0f;

  int i0 = s.index;
  int i1 = (s.index + 1);
  if (i1 >= s.length) i1 = 0;

  float y = 0.5f * (s.buffer[i0] + s.buffer[i1]);   // 简单平均
  y *= s.decay;

  s.buffer[i0] = y;
  s.index = i1;

  return y;
}

// 初始化指定和弦
void initChord(int chordIndex) {
  if (chordIndex < 0 || chordIndex >= NUM_CHORDS) return;

  MidiChord &ch = chords[chordIndex];
  int stringsToUse = (int)min((size_t)NUM_STRINGS, ch.count);

  for (int i = 0; i < stringsToUse; ++i) {
    float freq = midiToFreq(ch.notes[i]);
    initKSString(ksStrings[i], freq, 0.996f);  // 衰减略小于 1
  }
  // 多余的弦关掉
  for (int i = stringsToUse; i < NUM_STRINGS; ++i) {
    ksStrings[i].active = false;
  }

  samplesInChord = 0;

  Serial.print("Now playing chord: ");
  Serial.println(ch.name);
}

// 生成一个采样点（按照 4-5-3-6-2-5-1 进行）
float generateSample() {
  if (samplesInChord >= samplesPerChord) {
    samplesInChord = 0;
    // 进行推进到下一个和弦
    progPos = (progPos + 1) % PROG_LEN;
    currentChordIndex = progression[progPos];
    initChord(currentChordIndex);
  }
  samplesInChord++;

  float out = 0.0f;
  for (int i = 0; i < NUM_STRINGS; ++i) {
    if (ksStrings[i].active) {
      out += processKSString(ksStrings[i]);
    }
  }

  // 混音后整体音量
  const float masterGain = 0.4f;
  out *= masterGain;

  if (out > 1.0f)  out = 1.0f;
  if (out < -1.0f) out = -1.0f;

  return out;
}

// ================== I2S 初始化 ==================
void setupI2S() {
  const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,     // 立体声，左右同样数据
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = FRAMES_PER_BUFFER,
    .use_apll = false
  };

  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRCLK,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  esp_err_t err;
  err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("i2s_driver_install failed: %d\n", err);
    while (1) { delay(1000); }
  }

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("i2s_set_pin failed: %d\n", err);
    while (1) { delay(1000); }
  }

  i2s_zero_dma_buffer(I2S_PORT);
}

// ================== Arduino 入口 ==================
static int16_t i2sBuffer[FRAMES_PER_BUFFER * 2];  // *2 是左右声道

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32-S2 I2S Guitar-like 4536251 Chord Progression");

  randomSeed((uint32_t)esp_random());

  setupI2S();

  // 从进行的第一个和弦开始：F
  progPos = 0;
  currentChordIndex = progression[progPos];
  initChord(currentChordIndex);
}

void loop() {
  // 填一块 buffer 然后写到 I2S
  for (int n = 0; n < FRAMES_PER_BUFFER; ++n) {
    float s = generateSample();
    int16_t v = (int16_t)(s * 32760.0f);

    // 立体声左右一致
    i2sBuffer[2 * n]     = v;
    i2sBuffer[2 * n + 1] = v;
  }

  size_t bytesWritten = 0;
  i2s_write(I2S_PORT,
            (const char*)i2sBuffer,
            sizeof(i2sBuffer),
            &bytesWritten,
            portMAX_DELAY);
}
