# GuitarFX Pro - 全功能吉他工作站

> 立志做好国产的手机创作生态应用

## Context

CLI效果器原型验证了荣耀X70手机音频链路：
- USB输入(Focusrite Solo) + 扬声器输出 = 21ms延迟
- 0 xrun，ring buffer稳定
- AAudio直连（非Oboe/LV2），DaisySP效果器

## 当前架构

```
GuitarFXPro/
├── app/src/main/cpp/
│   ├── engine.cpp          ← AAudio全双工引擎 + JNI桥接
│   ├── effects.h/cpp       ← 效果链（DaisySP）
│   ├── audio_analyzer.h    ← 实时频谱分析（KissFFT算法）
│   ├── core/
│   │   ├── tuner.h/cpp     ← YIN调音器
│   │   ├── looper.h/cpp    ← Loop Station (8层叠录)
│   │   ├── audio_hal.h     ← 平台抽象接口
│   │   ├── sensor_hal.h
│   │   └── file_hal.h
│   ├── hal/                 ← Android实现
│   ├── DaisySP/             ← DSP效果器库 (Electrosmith)
│   └── kissfft/             ← KissFFT (Mark Borgerding/Bernd Porr)
└── app/src/main/java/       ← Kotlin UI
```

## 信号链

```
USB Input → Noise Gate → Compressor → EQ → Overdrive/Distortion/Fuzz
         → Chorus → Phaser → Tremolo → Delay → Reverb → Shimmer
         → Soft Clip → Speaker Output
              ↓                              ↓
         Tuner (YIN)              Audio Analyzer (KissFFT)
```

## 开源依赖

| 库 | 许可证 | 用途 |
|---|--------|------|
| [DaisySP](https://github.com/electro-smith/DaisySP) | MIT | DSP效果器 |
| [KissFFT](https://github.com/berndporr/kiss-fft) | BSD | FFT频谱分析 |
| [AAudio](https://developer.android.com/ndk/guides/audio/aaudio/aaudio) | Apache 2.0 | 低延迟音频 |

## 音频分析器参考

| 项目 | 许可证 | 吸收的核心算法 |
|------|--------|---------------|
| [bewantbe/audio-analyzer-for-android](https://github.com/bewantbe/audio-analyzer-for-android) | Apache 2.0 | STFT 50%重叠、窗口能量补偿、A-weighting、二次插值峰值、RMS归一化 |
| [borisRadonic/AudioAnalyzer](https://github.com/borisRadonic/AudioAnalyzer) | MIT | FFT幅度缩放、dBFS计算、Goertzel单频检测 |

## 实施阶段

### Phase 1：基础引擎 ✅ 已完成
- AAudio USB输入 + 扬声器输出，~27ms延迟
- DaisySP效果器：Overdrive、Chorus、Phaser、Tremolo、Wavefolder、Svf、Ladder
- 平台HAL抽象（Android/HarmonyOS ready）
- KissFFT实时频谱分析器

### Phase 2：调音器 ✅ 已完成
- YIN pitch detection
- JNI接口

### Phase 3：录音 + 伴奏（1天）
- recorder.cpp：从效果链输出捕获，写入WAV
- backing.cpp：解码WAV/MP3，混入主输出
- GUI：录音按钮、伴奏文件选择
- **验证**：录制带效果的吉他，播放伴奏同时弹奏

### Phase 4：鼓机（1-2天）
- drums.cpp：采样加载+pattern sequencer
- 预置kick/snare/hihat等采样
- 16步sequencer GUI
- BPM控制
- **验证**：鼓机+吉他同步播放

### Phase 5：Loop Station ✅ 已完成
- looper.cpp：录制/循环/叠录，最多8层
- 每层独立音量/静音
- Undo/Redo

### Phase 6：后摇预设 + 效果自动化（1-2天）
- 预设系统：一键加载后摇音色组合
  - Ambient Pad、Shimmer、Wall of Sound、Glacial、Crescendo
- automation.cpp：LFO（sine/tri/saw/sq）+ 包络跟随器
- LFO可路由到任意效果器参数
- **验证**：Shimmer音色，LFO自动调制delay参数

### Phase 7：动态乐谱（1-2天）
- 乐谱JSON格式定义
- YIN音高检测→乐谱推进
- 自定义View渲染简谱/五线谱
- 自动翻页
- **验证**：弹奏时乐谱跟随高亮

### Phase 8：传感器集成（1天）
- SensorManager注册陀螺仪/加速度/距离
- 传感器值→效果器参数映射
- 校准界面
- **验证**：倾斜手机控制wah，手掌靠近切换效果

### Phase 9：智能音色匹配（后台听歌→自动调效果器）（2-3天）
- **核心功能**：App后台运行，捕获手机正在播放的音频（Internal Audio Capture）
- **分析引擎**：用KissFFT实时频谱分析，提取音色特征：
  - 频谱包络（各频段能量比）
  - 失真程度（谐波含量/THD）
  - 混响特征（尾音衰减）
  - 延迟特征（回声间隔）
  - 调制特征（chorus/phaser的LFO频率）
- **音色匹配算法**：
  - 预置参考音色库（经典音色的频谱指纹）
  - 实时对比播放音频与参考音色
  - 自动调整：distortion drive/tone、EQ、reverb mix、delay time/feedback、chorus depth/rate
- **Android实现**：
  - Android 10+: `MediaProjection` + `AudioPlaybackCapture` 捕获内部音频
  - 后台Service保活（Foreground Service + 通知栏）
  - 实时分析+效果参数平滑过渡（避免突变爆音）
- **应用场景**：
  - 听到喜欢的吉他音色→一键复制到自己的效果器
  - 跟着伴奏自动调整匹配音色
  - 学习名曲时自动还原吉他手的效果器设置
- **验证**：播放一首吉他曲，App自动识别出大致的过载/延迟/混响参数

### Phase 10：打磨（1-2天）
- VU电平表
- 参数平滑
- CPU优化
- 长时间稳定性测试
- 开源发布（GPL许可证）

## 长期目标：复刻 Cubasis

参考 Cubasis 的功能，逐步扩展为完整移动端 DAW。

### 开源参考项目

| 项目 | 星数 | 语言 | 许可证 | 参考价值 |
|------|------|------|--------|---------|
| [Stargate DAW](https://github.com/stargatedaw/stargate) | 700+ | C/C++ | GPL | 最完整的Android DAW，多轨混音、内置合成器/效果器、MIDI、波形编辑，已上架Google Play。整体架构首选参考 |
| [Hydrogen](https://github.com/hydrogen-music/hydrogen) | 1.3k+ | C++ | GPL | 专业鼓机+音序器，Pattern-based编程、采样加载、swing/人性化节奏、MIDI。鼓机模块首选参考 |
| [LMMS](https://github.com/lmms/lmms) | 8k+ | C++ | GPL | 开源FL Studio替代品，Piano roll、自动化、效果器链、混音器。UI和功能设计参考 |
| [Ardour](https://github.com/Ardour/ardour) | 4k+ | C++ | GPL | 开源DAW金标准，专业混音引擎、routing、automation。混音器/录音引擎架构参考 |
| [PocketDAW](https://github.com/ferluht/pocketdaw) | - | Kotlin/C++ | - | Android DAW，模块化合成器架构 |
| [DawRio](https://github.com/ParsleyJ/dawrio) | - | Kotlin | - | 实验性Android DAW |
| [openDAW](https://github.com/andremichelle/openDAW) | - | TypeScript | - | Web DAW，下一代架构设计，UI/UX参考 |
| [Tracktion Engine](https://github.com/Tracktion/tracktion_engine) | 1k+ | C++ | GPL/Commercial | 纯C++ DAW引擎，可嵌入，JUCE框架。引擎架构参考 |

### Cubasis 功能对标

| Cubasis 功能 | 对应开源参考 | GuitarFXPro 计划 |
|-------------|-------------|-----------------|
| 多轨录音/混音 | Stargate, Ardour | Phase 3 录音 + 混音器 |
| 鼓机/音序器 | Hydrogen | Phase 4 鼓机 |
| MIDI Piano Roll | LMMS, Stargate | 长期目标 |
| 内置合成器 | Stargate, PocketDAW | 长期目标（已预留合成器模块） |
| 效果器插件 | DaisySP（已集成） | 已完成 |
| 调音器 | 已有YIN | Phase 2 已完成 |
| Loop Station | 已有8层叠录 | Phase 5 已完成 |
| 智能音色匹配 | KissFFT（已集成） | Phase 9 |
| Micro Tuner | - | 可参考 Stargate |
| 采样器 | Hydrogen | 可参考 Hydrogen |

## 开发环境

- Android Studio Ladybug+
- NDK r27c
- Kotlin / C++17
- minSdk 28
- 测试设备：荣耀X70
- 许可证：GPL-3.0
