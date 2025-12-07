#pragma once
//
// guitar_params.h
// ==============================
// 所有“可调参数”集中在这里：
//  - 采样率 / 弦数量 / buffer 尺寸
//  - 扫弦速度（弦与弦间的延时）
//  - KS 衰减（延音长短）
//  - 力度 → 音量/延音 映射
//  - 频响 / 箱体音色
//  - 总体输出音量
//  - 每根弦的 detune
//  - AutoKey（海阔天空版）顺序
//  - Choke（切音）的啪声参数
//

// -----------------------------------------------------------------------------
// 0. 基本引擎维度
// -----------------------------------------------------------------------------

// 音频采样率（Hz）
constexpr int kSampleRate = 16000;

// 虚拟弦数量（模拟吉他 6 根弦）
constexpr int kNumStrings = 6;

// Karplus–Strong 延迟线最大长度
constexpr int kMaxKsDelay = 512;

// 同时允许排队的最大 pluck 数
constexpr int kMaxScheduledPlucks = kNumStrings * 4;


// -----------------------------------------------------------------------------
// 1. 扫弦手感（弦与弦之间的时间间隔）
// -----------------------------------------------------------------------------
//
// 力度小 → 接近 kInterDelayMsSlow（慢扫）
// 力度大 → 接近 kInterDelayMsFast（快扫）
// 单位：ms
//
constexpr float kInterDelayMsSlow = 20.0f;  // 非常轻柔的慢扫
constexpr float kInterDelayMsFast = 4.0f;   // 大力扫时的“几乎同时”

// -----------------------------------------------------------------------------
// 2. KS 衰减（延音长短）
// -----------------------------------------------------------------------------
//
// 每次迭代乘以 decay，越接近 1 尾巴越长。
// 实际使用中：
//   decay = kKsDecayMin + (kKsDecayMax - kKsDecayMin) * velocityNorm;
//
constexpr float kKsDecayMin = 0.992f;    // 力度小的衰减
constexpr float kKsDecayMax = 0.9985f;   // 力度大的衰减


// -----------------------------------------------------------------------------
// 3. 拨弦噪声 RMS & 力度映射（响度/动态）
// -----------------------------------------------------------------------------
//
// initKSString() 会生成一段噪声，并归一化到 targetRms：
//   targetRms = kBaseNoiseTargetRms * velScale;
//   velScale  = kVelRmsScaleMin + (kVelRmsScaleMax - kVelRmsScaleMin)*v;
//
constexpr float kBaseNoiseTargetRms = 0.20f;  // 中等力度时基础 RMS
constexpr float kVelRmsScaleMin     = 0.5f;   // velocity ≈ 0
constexpr float kVelRmsScaleMax     = 1.4f;   // velocity ≈ 127


// -----------------------------------------------------------------------------
// 4. 六根弦 detune 配置（单位：cent）
// -----------------------------------------------------------------------------
//
// 每根弦相对于理论音高的轻微偏移，用来制造“厚度”。
// 顺序：从最低弦到最高弦（stringIndex = 0..5）。
//
constexpr float kDetuneCents[kNumStrings] = {
    -6.0f,  // 低音略低
    -3.0f,
    -1.5f,
    0.0f,   // 中间弦基本准
    2.0f,
    4.0f    // 高音略高
};


// -----------------------------------------------------------------------------
// 5. Tone Shaping：亮度、attack、箱体
// -----------------------------------------------------------------------------

// 高频低通响应速度（越大越“亮”）
constexpr float kLpToneAlpha = 0.24f;

// presence 占比（out 与低通差值的混合）
constexpr float kPresenceMix = 0.25f;

// 箱体低频滤波速度 & 占比
constexpr float kBodyAlpha   = 0.02f;
constexpr float kBodyMix     = 0.25f;


// -----------------------------------------------------------------------------
// 6. 总体输出音量（在此基础上再乘 gMasterVolume 0..1）
// -----------------------------------------------------------------------------
constexpr float kOutputGain  = 3.5f;


// -----------------------------------------------------------------------------
// 7. AutoKey 配置（晴天版）
// -----------------------------------------------------------------------------
//
// chords[] 排列约定（在 esp32_guitar_engine.ino 中实现）：
//  0:C,  1:G,  2:D,   3:A,   4:E,   5:F,
//  6:C7, 7:G7, 8:D7,  9:A7, 10:E7, 11:B7,
//  12:Am,13:Em,14:Dm,15:Bm,16:F#m,17:Gm,
//  18:Dsus4,19:Gsus4,20:Asus4,21:Esus4,22:Bdim,23:F#dim,
//  24:Cmaj7
//
// 下面先把常用和弦的索引写成常量，方便 AutoKey 序列阅读。

constexpr int CH_C      = 0;
constexpr int CH_G      = 1;
constexpr int CH_D      = 2;
constexpr int CH_A      = 3;
constexpr int CH_E      = 4;
constexpr int CH_F      = 5;
constexpr int CH_C7     = 6;
constexpr int CH_G7     = 7;
constexpr int CH_D7     = 8;
constexpr int CH_A7     = 9;
constexpr int CH_E7     = 10;
constexpr int CH_B7     = 11;
constexpr int CH_Am     = 12;
constexpr int CH_Em     = 13;
constexpr int CH_Dm     = 14;
constexpr int CH_Bm     = 15;
constexpr int CH_FsharpM= 16;  // F#m
constexpr int CH_Gm     = 17;
constexpr int CH_Dsus4  = 18;
constexpr int CH_Gsus4  = 19;
constexpr int CH_Asus4  = 20;
constexpr int CH_Esus4  = 21;
constexpr int CH_Bdim   = 22;
constexpr int CH_FsharpDim = 23;
constexpr int CH_Cmaj7  = 24;  // 新增 Cmaj7（放在 chords[] 末尾）

// -------- AutoKey --------
//
// 换成：
//   ① Em(3) Cmaj7(7) G(7) D(3)，整组轮 9 次
//   ② Em(3) Cmaj7(7) Dsus4(7) D(3)，轮 1 次
//   ③ G(10) Em(10) C(5) D(5)
//   ④ G(10) B7(10) Em(10) C(10) Dsus4(7) D(3)
//   ⑤ G(10) Em(10)
//   ⑥ C(5) D(5) G(10) B7(10) Em(10) C(10) Dsus4(7) D(3) G(10)
//
// 由于每一段里“轮几次”已经展开为重复的索引，
// 所以 kAutoKeyRepeatsPerChord = 1 即可。

#define REP3(x)    x,x,x
#define REP5(x)    x,x,x,x,x
#define REP7(x)    x,x,x,x,x,x,x
#define REP10(x)   x,x,x,x,x,x,x,x,x,x

constexpr int kAutoKeySeq[] = {
  // ---- Section 1：Em(3) Cmaj7(7) G(7) D(3)，整组循环 9 次 ----
  REP3(CH_Em),    REP7(CH_Cmaj7),  REP7(CH_G),  REP3(CH_D),
  REP3(CH_Em),    REP7(CH_Cmaj7),  REP7(CH_G),  REP3(CH_D),
  REP3(CH_Em),    REP7(CH_Cmaj7),  REP7(CH_G),  REP3(CH_D),
  REP3(CH_Em),    REP7(CH_Cmaj7),  REP7(CH_G),  REP3(CH_D),
  REP3(CH_Em),    REP7(CH_Cmaj7),  REP7(CH_G),  REP3(CH_D),
  REP3(CH_Em),    REP7(CH_Cmaj7),  REP7(CH_G),  REP3(CH_D),
  REP3(CH_Em),    REP7(CH_Cmaj7),  REP7(CH_G),  REP3(CH_D),
  REP3(CH_Em),    REP7(CH_Cmaj7),  REP7(CH_G),  REP3(CH_D),
  REP3(CH_Em),    REP7(CH_Cmaj7),  REP7(CH_G),  REP3(CH_D),

  // ---- Section 2：Em(3) Cmaj7(7) Dsus4(7) D(3) ----
  REP3(CH_Em),    REP7(CH_Cmaj7),  REP7(CH_Dsus4),  REP3(CH_D),

  // ---- Section 3：G(10) Em(10) C(5) D(5) ----
  REP10(CH_G),    REP10(CH_Em),   REP5(CH_C),   REP5(CH_D),

  // ---- Section 4：G(10) B7(10) Em(10) C(10) Dsus4(7) D(3) ----
  REP10(CH_G),    REP10(CH_B7),   REP10(CH_Em), REP10(CH_C),
  REP7(CH_Dsus4), REP3(CH_D),

  // ---- Section 5：G(10) Em(10) ----
  REP10(CH_G),    REP10(CH_Em),

  // ---- Section 6：
  // C(5) D(5) G(10) B7(10) Em(10) C(10) Dsus4(7) D(3) G(10)
  REP5(CH_C),     REP5(CH_D),
  REP10(CH_G),    REP10(CH_B7),   REP10(CH_Em), REP10(CH_C),
  REP7(CH_Dsus4), REP3(CH_D),
  REP10(CH_G),
};

#undef REP3
#undef REP5
#undef REP7
#undef REP10

// 每个元素只用一次；重复次数已经展开在数组里了
constexpr int kAutoKeySeqLength       =
    sizeof(kAutoKeySeq) / sizeof(kAutoKeySeq[0]);
constexpr int kAutoKeyRepeatsPerChord = 1;


// -----------------------------------------------------------------------------
// 8. Choke（切音/拍弦）啪声参数
// -----------------------------------------------------------------------------
//
// 切音时：
//  - 立刻停止所有弦（active = false）
//  - 开启一个极短的噪声包络，用来模拟“啪”的瞬态。
//  - 长度 10~30ms，Env 每个 sample 乘一个系数快速衰减。
//

// 噪声长度（samples）：这里设为 20ms
constexpr int   kChokeLengthSamples = (int)(0.020f * kSampleRate);

// 噪声基准幅度（0~1，可根据耳朵调整）
constexpr float kChokeBaseAmp   = 0.9f;

// 包络衰减系数（每个 sample 乘一次）
constexpr float kChokeEnvDecay  = 0.90f;
