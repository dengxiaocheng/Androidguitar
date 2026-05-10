package com.guitarfx.audio

object AudioBridge {

    init {
        System.loadLibrary("guitarfx")
    }

    // Engine lifecycle
    external fun nativeStart(outDev: Int): Boolean
    external fun nativeStop()
    external fun nativeIsRunning(): Boolean

    // Master controls
    external fun nativeSetGain(gain: Float)
    external fun nativeSetVolume(vol: Float)
    external fun nativeSetBypass(bypass: Boolean)

    // Effect parameters
    external fun nativeSetDistortion(mode: Int, drive: Float, tone: Float)
    external fun nativeSetEQ(low: Float, mid: Float, high: Float)
    external fun nativeSetDelay(timeMs: Float, feedback: Float, mix: Float)
    external fun nativeSetReverb(roomSize: Float, damping: Float, mix: Float)
    external fun nativeSetNoiseGate(threshold: Float)
    external fun nativeSetChorus(rate: Float, depth: Float, mix: Float)
    external fun nativeSetShimmer(roomSize: Float, mix: Float, shimmer: Float)

    // Effect enable/disable
    external fun nativeSetEffectEnabled(effectId: Int, enabled: Boolean)

    // Presets
    external fun nativeLoadPreset(presetId: Int)

    // Monitoring
    external fun nativeGetPeakIn(): Float
    external fun nativeGetPeakOut(): Float
    external fun nativeGetXruns(): Int

    // Tuner
    external fun nativeGetTunerFreq(): Float
    external fun nativeGetTunerNote(): Int
    external fun nativeGetTunerCents(): Float

    // Sensor
    external fun nativeSetSensorValue(sensorType: Int, value: Float)

    // Effect IDs
    const val FX_GATE = 0
    const val FX_COMP = 1
    const val FX_EQ = 2
    const val FX_DIST = 3
    const val FX_CHORUS = 4
    const val FX_DELAY = 5
    const val FX_REVERB = 6
    const val FX_SHIMMER = 7

    // Preset IDs
    const val PRESET_CLEAN = 0
    const val PRESET_OVERDRIVE = 1
    const val PRESET_DISTORTION = 2
    const val PRESET_FUZZ = 3
    const val PRESET_POST_ROCK = 4
    const val PRESET_WALL_OF_SOUND = 5
}
