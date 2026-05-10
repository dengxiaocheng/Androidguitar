#include <jni.h>
#include <android/log.h>
#include <aaudio/AAudio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include "effects.h"
#include "tuner.h"
#include "audio_analyzer.h"

#define LOG_TAG "GuitarFX"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define RB_SIZE (FX_SR * 2)

// ---- Global state ----
static FXChain g_fx;
static Tuner g_tuner;
static float g_ring_buf[RB_SIZE];
static volatile int32_t g_rb_write = 0, g_rb_read = 0;
static volatile int g_output_synced = 0;
static AAudioStream *g_in_stream = nullptr;
static AAudioStream *g_out_stream = nullptr;

// ---- Audio Analyzer ----
static analyzer::AudioAnalyzer g_analyzer_in;   // input analysis
static analyzer::AudioAnalyzer g_analyzer_out;  // output analysis
static volatile int g_analyzer_frame = 0;

// ---- Audio callbacks ----
static aaudio_data_callback_result_t on_input(
        AAudioStream *s, void *u, void *data, int32_t nf) {
    (void)s; (void)u;
    const float *in = (const float *)data;
    g_fx.in_frames += nf;
    for (int i = 0; i < nf; i++) {
        g_ring_buf[g_rb_write % RB_SIZE] = in[i];
        // Feed tuner
        tuner_push_sample(&g_tuner, in[i] * g_fx.input_gain);
        g_rb_write++;
    }
    return g_fx.running ? AAUDIO_CALLBACK_RESULT_CONTINUE
                        : AAUDIO_CALLBACK_RESULT_STOP;
}

static aaudio_data_callback_result_t on_output(
        AAudioStream *s, void *u, void *data, int32_t nf) {
    (void)s; (void)u;
    float *out = (float *)data;

    if (!g_output_synced) {
        if (g_rb_write < nf * 2) {
            memset(out, 0, nf * sizeof(float));
            g_fx.out_frames += nf;
            return AAUDIO_CALLBACK_RESULT_CONTINUE;
        }
        g_rb_read = g_rb_write - nf * 2;
        g_output_synced = 1;
    }

    int32_t avail = g_rb_write - g_rb_read;
    if (avail > nf * 4) {
        g_rb_read += nf;
        avail = g_rb_write - g_rb_read;
    }

    if (avail >= nf) {
        for (int i = 0; i < nf; i++) {
            float raw = g_ring_buf[g_rb_read % RB_SIZE];
            float processed = fxchain_process(&g_fx, raw);
            out[i] = processed;
            g_rb_read++;

            // Feed analyzers
            g_analyzer_in.push(raw * g_fx.input_gain);
            if (g_analyzer_out.push(processed))
                g_analyzer_frame++;
        }
    } else {
        memset(out, 0, nf * sizeof(float));
        g_fx.xruns++;
        if (g_rb_write >= nf * 2) g_rb_read = g_rb_write - nf;
    }

    g_fx.out_frames += nf;

    // Log analyzer data every ~1 second (when new FFT frame is ready)
    static int last_analyzer_frame = 0;
    if (g_analyzer_frame > last_analyzer_frame) {
        last_analyzer_frame = g_analyzer_frame;
        const auto &sin = g_analyzer_in.get_stats();
        const auto &sout = g_analyzer_out.get_stats();
        LOGI("ANA_IN  rms=%.4f pk=%.4f dc=%.4f clips=%d zcr=%.3f "
             "bands=%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f "
             "thd=%.2f snr=%.1fdB noise=%.4f",
             sin.rms, sin.peak, sin.dc_offset, sin.clip_count, sin.zcr,
             sin.band_energy[0], sin.band_energy[1], sin.band_energy[2], sin.band_energy[3],
             sin.band_energy[4], sin.band_energy[5], sin.band_energy[6], sin.band_energy[7],
             sin.thd_estimate, sin.snr_db, sin.noise_floor_db);
        LOGI("ANA_OUT rms=%.4f pk=%.4f dc=%.4f clips=%d zcr=%.3f "
             "bands=%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f "
             "thd=%.2f snr=%.1fdB dyn=%.1fdB xruns=%d "
             "fx: g=%d c=%d e=%d d=%d ch=%d ph=%d tr=%d dl=%d rv=%d sh=%d gain=%.1f vol=%.1f",
             sout.rms, sout.peak, sout.dc_offset, sout.clip_count, sout.zcr,
             sout.band_energy[0], sout.band_energy[1], sout.band_energy[2], sout.band_energy[3],
             sout.band_energy[4], sout.band_energy[5], sout.band_energy[6], sout.band_energy[7],
             sout.thd_estimate, sout.snr_db, sout.dynamic_range_db, g_fx.xruns,
             g_fx.gate_on, g_fx.comp_on, g_fx.eq_on, g_fx.dist_on,
             g_fx.chorus_on, g_fx.phaser_on, g_fx.tremolo_on,
             g_fx.delay_on, g_fx.reverb_on, g_fx.shimmer_on,
             g_fx.input_gain, g_fx.output_vol);
    }

    return g_fx.running ? AAUDIO_CALLBACK_RESULT_CONTINUE
                        : AAUDIO_CALLBACK_RESULT_STOP;
}

static void on_error(AAudioStream *s, void *u, aaudio_result_t err) {
    (void)s; (void)u;
    LOGE("Stream error: %s", AAudio_convertResultToText(err));
    g_fx.running = 0;
}

// ---- JNI: Engine lifecycle ----

extern "C" JNIEXPORT jboolean JNICALL
Java_com_guitarfx_audio_AudioBridge_nativeStart(JNIEnv *env, jobject thiz, jint out_dev) {
    (void)env; (void)thiz;

    if (g_fx.running) return JNI_TRUE;

    fxchain_init(&g_fx);
    tuner_init(&g_tuner);
    g_analyzer_in.init(FX_SR);
    g_analyzer_out.init(FX_SR);
    g_analyzer_frame = 0;
    g_rb_write = g_rb_read = 0;
    g_output_synced = 0;
    g_fx.running = 1;

    aaudio_result_t r;

    // Input stream (USB)
    {
        AAudioStreamBuilder *b;
        AAudio_createStreamBuilder(&b);
        AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_INPUT);
        AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_EXCLUSIVE);
        AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
        AAudioStreamBuilder_setSampleRate(b, FX_SR);
        AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
        AAudioStreamBuilder_setChannelCount(b, 1);
        AAudioStreamBuilder_setDataCallback(b, on_input, nullptr);
        AAudioStreamBuilder_setErrorCallback(b, on_error, nullptr);
        r = AAudioStreamBuilder_openStream(b, &g_in_stream);
        AAudioStreamBuilder_delete(b);
        if (r != AAUDIO_OK) {
            LOGE("Input open failed: %s", AAudio_convertResultToText(r));
            g_fx.running = 0;
            return JNI_FALSE;
        }
    }

    // Output stream (speaker or USB)
    {
        AAudioStreamBuilder *b;
        AAudio_createStreamBuilder(&b);
        AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
        AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
        AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
        AAudioStreamBuilder_setSampleRate(b, FX_SR);
        AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
        AAudioStreamBuilder_setChannelCount(b, 1);
        if (out_dev > 0) AAudioStreamBuilder_setDeviceId(b, out_dev);
        AAudioStreamBuilder_setDataCallback(b, on_output, nullptr);
        AAudioStreamBuilder_setErrorCallback(b, on_error, nullptr);
        r = AAudioStreamBuilder_openStream(b, &g_out_stream);
        AAudioStreamBuilder_delete(b);
        if (r != AAUDIO_OK) {
            LOGE("Output open failed: %s", AAudio_convertResultToText(r));
            AAudioStream_close(g_in_stream);
            g_in_stream = nullptr;
            g_fx.running = 0;
            return JNI_FALSE;
        }
    }

    int in_burst = AAudioStream_getFramesPerBurst(g_in_stream);
    int out_burst = AAudioStream_getFramesPerBurst(g_out_stream);
    AAudioStream_setBufferSizeInFrames(g_in_stream, in_burst);
    AAudioStream_setBufferSizeInFrames(g_out_stream, out_burst);

    LOGI("Streams opened: in_burst=%d out_burst=%d out_dev=%d", in_burst, out_burst, out_dev);

    AAudioStream_requestStart(g_in_stream);
    AAudioStream_requestStart(g_out_stream);
    usleep(200000);

    LOGI("Audio engine started");
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_guitarfx_audio_AudioBridge_nativeStop(JNIEnv *env, jobject thiz) {
    (void)env; (void)thiz;
    if (!g_fx.running) return;

    g_fx.running = 0;
    usleep(100000);

    if (g_out_stream) {
        AAudioStream_requestStop(g_out_stream);
        AAudioStream_close(g_out_stream);
        g_out_stream = nullptr;
    }
    if (g_in_stream) {
        AAudioStream_requestStop(g_in_stream);
        AAudioStream_close(g_in_stream);
        g_in_stream = nullptr;
    }

    fxchain_free(&g_fx);
    LOGI("Audio engine stopped");
}

// ---- JNI: Effect parameters ----

extern "C" JNIEXPORT void JNICALL
Java_com_guitarfx_audio_AudioBridge_nativeSetGain(JNIEnv *env, jobject thiz, jfloat gain) {
    (void)env; (void)thiz;
    g_fx.input_gain = gain;
}

extern "C" JNIEXPORT void JNICALL
Java_com_guitarfx_audio_AudioBridge_nativeSetVolume(JNIEnv *env, jobject thiz, jfloat vol) {
    (void)env; (void)thiz;
    g_fx.output_vol = vol;
}

extern "C" JNIEXPORT void JNICALL
Java_com_guitarfx_audio_AudioBridge_nativeSetBypass(JNIEnv *env, jobject thiz, jboolean bypass) {
    (void)env; (void)thiz;
    g_fx.bypass = bypass ? 1 : 0;
}

extern "C" JNIEXPORT void JNICALL
Java_com_guitarfx_audio_AudioBridge_nativeSetDistortion(JNIEnv *env, jobject thiz,
        jint mode, jfloat drive, jfloat tone) {
    (void)env; (void)thiz;
    g_fx.dist.mode = mode;
    g_fx.dist.drive = drive;
    g_fx.dist.tone = tone;
}

extern "C" JNIEXPORT void JNICALL
Java_com_guitarfx_audio_AudioBridge_nativeSetEQ(JNIEnv *env, jobject thiz,
        jfloat low, jfloat mid, jfloat high) {
    (void)env; (void)thiz;
    eq_init(&g_fx.eq, low, mid, high);
}

extern "C" JNIEXPORT void JNICALL
Java_com_guitarfx_audio_AudioBridge_nativeSetDelay(JNIEnv *env, jobject thiz,
        jfloat time_ms, jfloat feedback, jfloat mix) {
    (void)env; (void)thiz;
    g_fx.delay.delay_ms = time_ms;
    g_fx.delay.feedback = feedback;
    g_fx.delay.mix = mix;
}

extern "C" JNIEXPORT void JNICALL
Java_com_guitarfx_audio_AudioBridge_nativeSetReverb(JNIEnv *env, jobject thiz,
        jfloat room_size, jfloat damping, jfloat mix) {
    (void)env; (void)thiz;
    // Re-initialize reverb with new params
    reverb_free(&g_fx.reverb);
    reverb_init(&g_fx.reverb, room_size, damping, mix);
}

extern "C" JNIEXPORT void JNICALL
Java_com_guitarfx_audio_AudioBridge_nativeSetNoiseGate(JNIEnv *env, jobject thiz, jfloat threshold) {
    (void)env; (void)thiz;
    g_fx.gate.threshold = threshold;
    g_fx.gate.gated = 0;
}

extern "C" JNIEXPORT void JNICALL
Java_com_guitarfx_audio_AudioBridge_nativeSetChorus(JNIEnv *env, jobject thiz,
        jfloat rate, jfloat depth, jfloat mix) {
    (void)env; (void)thiz;
    chorus_init(&g_fx.chorus, rate, depth, mix);
}

extern "C" JNIEXPORT void JNICALL
Java_com_guitarfx_audio_AudioBridge_nativeSetShimmer(JNIEnv *env, jobject thiz,
        jfloat room_size, jfloat mix, jfloat shimmer) {
    (void)env; (void)thiz;
    shimmer_free(&g_fx.shimmer);
    shimmer_init(&g_fx.shimmer, room_size, 0.2f, mix, shimmer);
}

// ---- JNI: Effect enable/disable ----

extern "C" JNIEXPORT void JNICALL
Java_com_guitarfx_audio_AudioBridge_nativeSetEffectEnabled(JNIEnv *env, jobject thiz,
        jint effect_id, jboolean enabled) {
    (void)env; (void)thiz;
    int on = enabled ? 1 : 0;
    switch (effect_id) {
        case 0: g_fx.gate_on = on; break;
        case 1: g_fx.comp_on = on; break;
        case 2: g_fx.eq_on = on; break;
        case 3: g_fx.dist_on = on; break;
        case 4: g_fx.chorus_on = on; break;
        case 5: g_fx.delay_on = on; break;
        case 6: g_fx.reverb_on = on; break;
        case 7: g_fx.shimmer_on = on; break;
        case 8: g_fx.phaser_on = on; break;
        case 9: g_fx.tremolo_on = on; break;
    }
}

// ---- JNI: Presets ----

extern "C" JNIEXPORT void JNICALL
Java_com_guitarfx_audio_AudioBridge_nativeLoadPreset(JNIEnv *env, jobject thiz, jint preset_id) {
    (void)env; (void)thiz;

    // Reset all effects first
    g_fx.gate_on = 0; g_fx.comp_on = 0; g_fx.eq_on = 0;
    g_fx.dist_on = 0; g_fx.chorus_on = 0; g_fx.phaser_on = 0; g_fx.tremolo_on = 0;
    g_fx.delay_on = 0; g_fx.reverb_on = 0; g_fx.shimmer_on = 0;

    switch (preset_id) {
        case 0: // Clean
            g_fx.input_gain = 3.0f;
            g_fx.eq_on = 1;
            eq_init(&g_fx.eq, 0, 2.0f, 0);
            break;
        case 1: // Overdrive
            g_fx.input_gain = 4.0f;
            g_fx.eq_on = 1;
            g_fx.dist_on = 1;
            eq_init(&g_fx.eq, 2.0f, -1.0f, 3.0f);
            dist_init(&g_fx.dist, 5.0f, 0.6f, 1);
            break;
        case 2: // Distortion
            g_fx.input_gain = 5.0f;
            g_fx.eq_on = 1;
            g_fx.dist_on = 1;
            eq_init(&g_fx.eq, 2.0f, -1.0f, 4.0f);
            dist_init(&g_fx.dist, 8.0f, 0.5f, 2);
            break;
        case 3: // Fuzz
            g_fx.input_gain = 5.0f;
            g_fx.eq_on = 1;
            g_fx.dist_on = 1;
            eq_init(&g_fx.eq, 3.0f, -2.0f, 5.0f);
            dist_init(&g_fx.dist, 12.0f, 0.4f, 3);
            break;
        case 4: // Post-Rock Ambient
            g_fx.input_gain = 4.0f;
            g_fx.eq_on = 1;
            g_fx.chorus_on = 1;
            g_fx.delay_on = 1;
            g_fx.reverb_on = 1;
            g_fx.shimmer_on = 1;
            eq_init(&g_fx.eq, 1.0f, 0, 2.0f);
            chorus_init(&g_fx.chorus, 1.0f, 1.0f, 0.4f);
            delay_init(&g_fx.delay, 400.0f, 0.5f, 0.4f);
            reverb_free(&g_fx.reverb);
            reverb_init(&g_fx.reverb, 0.8f, 0.2f, 0.5f);
            shimmer_init(&g_fx.shimmer, 0.8f, 0.2f, 0.4f, 0.6f);
            break;
        case 5: // Wall of Sound
            g_fx.input_gain = 4.0f;
            g_fx.eq_on = 1;
            g_fx.dist_on = 1;
            g_fx.delay_on = 1;
            g_fx.reverb_on = 1;
            g_fx.shimmer_on = 1;
            eq_init(&g_fx.eq, 2.0f, 0, 3.0f);
            dist_init(&g_fx.dist, 3.0f, 0.5f, 1);
            delay_init(&g_fx.delay, 500.0f, 0.6f, 0.5f);
            reverb_free(&g_fx.reverb);
            reverb_init(&g_fx.reverb, 1.0f, 0.15f, 0.6f);
            shimmer_init(&g_fx.shimmer, 1.0f, 0.1f, 0.5f, 0.7f);
            break;
    }
}

// ---- JNI: Monitoring ----

extern "C" JNIEXPORT jfloat JNICALL
Java_com_guitarfx_audio_AudioBridge_nativeGetPeakIn(JNIEnv *env, jobject thiz) {
    (void)env; (void)thiz;
    float p = g_fx.peak_in;
    g_fx.peak_in = 0;
    return p;
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_guitarfx_audio_AudioBridge_nativeGetPeakOut(JNIEnv *env, jobject thiz) {
    (void)env; (void)thiz;
    float p = g_fx.peak_out;
    g_fx.peak_out = 0;
    return p;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_guitarfx_audio_AudioBridge_nativeGetXruns(JNIEnv *env, jobject thiz) {
    (void)env; (void)thiz;
    return g_fx.xruns;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_guitarfx_audio_AudioBridge_nativeIsRunning(JNIEnv *env, jobject thiz) {
    (void)env; (void)thiz;
    return g_fx.running ? JNI_TRUE : JNI_FALSE;
}

// ---- JNI: Tuner ----

extern "C" JNIEXPORT jfloat JNICALL
Java_com_guitarfx_audio_AudioBridge_nativeGetTunerFreq(JNIEnv *env, jobject thiz) {
    (void)env; (void)thiz;
    tuner_detect(&g_tuner);
    return g_tuner.detected_freq;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_guitarfx_audio_AudioBridge_nativeGetTunerNote(JNIEnv *env, jobject thiz) {
    (void)env; (void)thiz;
    return g_tuner.detected_note;
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_guitarfx_audio_AudioBridge_nativeGetTunerCents(JNIEnv *env, jobject thiz) {
    (void)env; (void)thiz;
    return g_tuner.cents_off;
}

// ---- JNI: Sensor bridge ----

extern "C" JNIEXPORT void JNICALL
Java_com_guitarfx_audio_AudioBridge_nativeSetSensorValue(JNIEnv *env, jobject thiz,
        jint sensor_type, jfloat value) {
    (void)env; (void)thiz;
    switch (sensor_type) {
        case 0:
            if (g_fx.chorus.initialized) {
                float depth = fabsf(value) * 0.8f;
                if (depth > 1.0f) depth = 1.0f;
                g_fx.chorus.daisy_chorus.SetLfoDepth(depth, depth);
            }
            break;
        case 1: break;
        case 2:
            if (value < 1.0f) g_fx.bypass = !g_fx.bypass;
            break;
    }
}

// ---- JNI: Stats (from analyzer) ----

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_guitarfx_audio_AudioBridge_nativeGetStats(JNIEnv *env, jobject thiz) {
    (void)thiz;
    const auto &sin = g_analyzer_in.get_stats();
    const auto &sout = g_analyzer_out.get_stats();
    float stats[24];
    stats[0] = sin.rms;
    stats[1] = sin.peak;
    stats[2] = sin.dc_offset;
    stats[3] = (float)sin.clip_count;
    stats[4] = sin.thd_estimate;
    stats[5] = sin.snr_db;
    stats[6] = sin.noise_floor_db;
    stats[7] = sout.rms;
    stats[8] = sout.peak;
    stats[9] = sout.dc_offset;
    stats[10] = (float)sout.clip_count;
    stats[11] = sout.thd_estimate;
    stats[12] = sout.snr_db;
    stats[13] = sout.dynamic_range_db;
    // Output band energies
    for (int i = 0; i < 8; i++) stats[14 + i] = sout.band_energy[i];
    stats[22] = (float)g_fx.xruns;
    stats[23] = sin.zcr;
    jfloatArray arr = env->NewFloatArray(24);
    env->SetFloatArrayRegion(arr, 0, 24, stats);
    return arr;
}
