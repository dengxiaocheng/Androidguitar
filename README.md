# GuitarFX Pro

Android guitar effects processor with real-time audio analysis.

## Features

- Real-time guitar effects processing via AAudio (USB input + speaker output)
- DaisySP DSP library integration (Overdrive, Chorus, Phaser, Tremolo, Wavefolder, Svf, Ladder)
- KissFFT real-time spectrum analysis (8-band, THD/SNR/noise floor)
- YIN pitch detection (tuner)
- Loop Station (8-layer overdub)
- Platform HAL abstraction (Android/HarmonyOS)
- ~27ms latency on Honor X70

## Signal Chain

```
USB Input → Noise Gate → Compressor → EQ → Distortion → Chorus → Phaser
         → Tremolo → Delay → Reverb → Shimmer → Soft Clip → Speaker Output
              ↓                                        ↓
         Tuner (YIN)                          Audio Analyzer (KissFFT)
```

## Build

```bash
export JAVA_HOME="/path/to/android-studio/jbr"
gradle assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Requirements: Android Studio, NDK r27c, minSdk 28

## Tech Stack

- C++17 (NDK) / Kotlin
- AAudio (low-latency audio)
- [DaisySP](https://github.com/electro-smith/DaisySP) (DSP effects)
- [KissFFT](https://github.com/berndporr/kiss-fft) (spectrum analysis)

## License

GPL-3.0
