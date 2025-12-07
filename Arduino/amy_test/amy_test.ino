#include <Arduino.h>
#include <AMY-Arduino.h>

// -------- I2S 引脚配置 --------
// 根据你的硬件调整，这里是参考值
//#define I2S_MCLK   7
#define I2S_BCLK   18
#define I2S_LRCLK  17
#define I2S_DOUT   14
//#define I2S_DIN    14

// 吉他 patch 号（从 https://shorepine.github.io/amy/ 选）
#define GUITAR_PATCH  242

// ========== 几个示例和弦 (MIDI note, A4=69) ==========
const uint8_t CHORD_CMAJ[] = {60, 64, 67};   // C4 E4 G4
const uint8_t CHORD_GMAJ[] = {55, 59, 62};   // G3 B3 D4
const uint8_t CHORD_AMIN[] = {57, 60, 64};   // A3 C4 E4
const uint8_t CHORD_EMIN[] = {52, 55, 59};   // E3 G3 B3

// ========== 发送 MIDI 音符事件 ==========
void sendNoteEvent(uint8_t voiceIndex, uint8_t midiNote, bool noteOn) {
  amy_event e = amy_default_event();
  e.synth = 1;                          // 使用 synth 1
  e.midi_note = midiNote;
  e.velocity = noteOn ? 1.0f : 0.0f;   // 1.0 = Note On, 0.0 = Note Off
  e.osc = voiceIndex;                  // 指定 oscillator
  amy_add_event(&e);
}

// ========== 播放和弦 ==========
void playChord(const uint8_t* notes, size_t nNotes, uint32_t holdMs) {
  // Note On：所有音符同时发声
  for (size_t i = 0; i < nNotes; ++i) {
    sendNoteEvent(i, notes[i], true);
  }

  delay(holdMs);

  // Note Off：所有音符同时释放
  for (size_t i = 0; i < nNotes; ++i) {
    sendNoteEvent(i, notes[i], false);
  }

  // 空隙时间，避免和弦重叠
  delay(80);
}

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  
  Serial.println("AMY Guitar Chord Test - ESP32-S2");

  // ---- 配置 AMY ----
  amy_config_t amy_config = amy_default_config();
  
  // I2S 引脚配置
  //amy_config.i2s_mclk = I2S_MCLK;
  amy_config.i2s_bclk = I2S_BCLK;
  amy_config.i2s_lrc = I2S_LRCLK;
  amy_config.i2s_dout = I2S_DOUT;
 //amy_config.i2s_din = I2S_DIN;
  
  // 启用默认合成器（MIDI channel 1, 2, 10）
  amy_config.features.default_synths = 1;
  
  // 启动 bleep 声提示初始化完成
  amy_config.features.startup_bleep = 1;

  // 启动 AMY
  amy_start(amy_config);
  amy_live_start();

  // ---- 配置吉他音色 ----
  amy_event e = amy_default_event();
  e.synth = 1;
  e.patch_number = GUITAR_PATCH;
  e.num_voices = 8;  // 最多 8 个和弦音
  amy_add_event(&e);

  Serial.println("Ready to play chords!");
  delay(500);
}

// ========== LOOP ==========
void loop() {
  // 必须持续调用 amy_update()
  amy_update();

  // 循环播放几个和弦
  playChord(CHORD_CMAJ, sizeof(CHORD_CMAJ) / sizeof(CHORD_CMAJ[0]), 600);
  playChord(CHORD_GMAJ, sizeof(CHORD_GMAJ) / sizeof(CHORD_GMAJ[0]), 600);
  playChord(CHORD_AMIN, sizeof(CHORD_AMIN) / sizeof(CHORD_AMIN[0]), 600);
  playChord(CHORD_EMIN, sizeof(CHORD_EMIN) / sizeof(CHORD_EMIN[0]), 600);

  delay(800);
}