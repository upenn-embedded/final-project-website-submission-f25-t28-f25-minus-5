#include <Arduino.h>
#include "driver/i2s.h"
#include <math.h>

// ----------- I2S & 采样参数 -----------
static const int SAMPLE_RATE = 44100;           // 44.1 kHz
static const i2s_port_t I2S_PORT = I2S_NUM_0;

#define I2S_BCK_PIN   18    // BCLK  → MAX98357 BCLK
#define I2S_WS_PIN    17    // LRCLK → MAX98357 LRC
#define I2S_DATA_PIN  14    // DATA  → MAX98357 DIN

static const int FRAMES_PER_BUFFER = 256;       // 每次写 256 个采样
int16_t sampleBuffer[FRAMES_PER_BUFFER];

const float PI_F = 3.14159265f;

// ----------- 和弦定义（简单三和弦）-----------
struct Chord {
  const char* name;
  float f1, f2, f3;   // 根音、三度、五度的频率
};

// 标准音高近似值
Chord chords[] = {
  //   名字   根音      三度      五度
  { "C",   261.63f, 329.63f, 392.00f },   // C E G
  { "G",   196.00f, 246.94f, 392.00f },   // G B D(近似用 G 的高八度)
  { "Am",  220.00f, 261.63f, 329.63f },   // A C E
  { "F",   174.61f, 220.00f, 349.23f }    // F A C
};

static const int NUM_CHORDS = sizeof(chords) / sizeof(chords[0]);

// 每个和弦持续时间（秒）
const float CHORD_DURATION_SEC = 2.0f;
// Attack / Release（秒）
const float ATTACK_TIME_SEC  = 0.02f;
const float RELEASE_TIME_SEC = 0.5f;

// 当前和弦与在该和弦内的采样计数
uint32_t samplesInChord = 0;
int currentChord = 0;

// ----------- I2S 初始化 -----------
void i2sSetup() {
  // I2S 配置（Tx 主机模式，16-bit 单声道）
  const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = FRAMES_PER_BUFFER,
    .use_apll = false
  };

  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCK_PIN,
    .ws_io_num = I2S_WS_PIN,
    .data_out_num = I2S_DATA_PIN,
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

// ----------- 生成当前和弦的一块音频数据 -----------
void fillChordBuffer() {
  uint32_t maxSamplesPerChord =
      (uint32_t)(CHORD_DURATION_SEC * SAMPLE_RATE);

  for (int i = 0; i < FRAMES_PER_BUFFER; ++i) {
    float t = (float)samplesInChord / (float)SAMPLE_RATE;  // 当前和弦内时间（秒）
    Chord &c = chords[currentChord];

    // ---- 简单 ADSR 包络（只有 Attack + Release）----
    float env = 1.0f;

    // Attack：线性从 0 → 1
    if (t < ATTACK_TIME_SEC) {
      env = t / ATTACK_TIME_SEC;
    }
    // Release：最后 RELEASE_TIME_SEC 秒线性衰减到 0
    else if (t > (CHORD_DURATION_SEC - RELEASE_TIME_SEC)) {
      float tail = CHORD_DURATION_SEC - t;
      if (tail < 0.0f) tail = 0.0f;
      env = tail / RELEASE_TIME_SEC;
    }

    // ---- 三个正弦波叠加构成和弦 ----
    float s = 0.0f;
    s += sinf(2.0f * PI_F * c.f1 * t);
    s += sinf(2.0f * PI_F * c.f2 * t);
    s += sinf(2.0f * PI_F * c.f3 * t);

    // 平均一下避免过载，再乘 envelope 调整响度
    s = (s / 3.0f) * env;

    // 音量缩放：0.0 ~ 1.0 → int16_t
    int16_t sample = (int16_t)(s * 30000.0f);  // 预留一点 headroom

    sampleBuffer[i] = sample;

    // ---- 计数 & 切换和弦 ----
    samplesInChord++;
    if (samplesInChord >= maxSamplesPerChord) {
      samplesInChord = 0;
      currentChord = (currentChord + 1) % NUM_CHORDS;
      Serial.printf("Now playing chord: %s\n", chords[currentChord].name);
    }
  }
}

// ----------- Arduino 入口 -----------
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32-S2 I2S Guitar Chord Test");

  i2sSetup();
}

void loop() {
  fillChordBuffer();

  size_t bytesWritten = 0;
  // 把缓冲区写到 I2S（阻塞直到 DMA 有空间）
  i2s_write(I2S_PORT,
            (const void*)sampleBuffer,
            sizeof(sampleBuffer),
            &bytesWritten,
            portMAX_DELAY);
}
