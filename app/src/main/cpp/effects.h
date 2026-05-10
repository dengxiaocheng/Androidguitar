#pragma once
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "daisysp.h"

#define FX_SR 48000
#define FX_PI 3.14159265358979f

// ---- Noise Gate ----
typedef struct {
    float threshold;
    float attack;
    float release;
    float envelope;
    int gated;
} NoiseGate;

void gate_init(NoiseGate *g, float threshold, float attack_ms, float release_ms);

static inline float gate_process(NoiseGate *g, float x) {
    float abs_x = fabsf(x);
    if (abs_x > g->envelope)
        g->envelope = g->attack * g->envelope + (1.0f - g->attack) * abs_x;
    else
        g->envelope = g->release * g->envelope + (1.0f - g->release) * abs_x;
    if (g->envelope < g->threshold) {
        g->gated++;
        if (g->gated > 64) return 0.0f;
        return x * (g->envelope / g->threshold);
    }
    g->gated = 0;
    return x;
}

// ---- 3-Band EQ (series biquad peaking - correct implementation) ----
typedef struct {
    float freq;
    float gain_db;
    float x1, x2, y1, y2;
    float b0, b1, b2, a1, a2;
} BiquadBand;

typedef struct {
    BiquadBand low, mid, high;
} Equalizer;

void eq_init(Equalizer *eq, float low_db, float mid_db, float high_db);

static inline float biquad_process(BiquadBand *b, float x) {
    float y = b->b0 * x + b->b1 * b->x1 + b->b2 * b->x2
              - b->a1 * b->y1 - b->a2 * b->y2;
    b->x2 = b->x1; b->x1 = x;
    b->y2 = b->y1; b->y1 = y;
    return y;
}

static inline float eq_process(Equalizer *eq, float x) {
    x = biquad_process(&eq->low, x);
    x = biquad_process(&eq->mid, x);
    x = biquad_process(&eq->high, x);
    return x;
}

// ---- Distortion (DaisySP Overdrive + Wavefolder + Svf tone) ----
typedef struct {
    float drive;
    float tone;
    float mix;
    int mode; // 0=clean, 1=overdrive, 2=distortion, 3=fuzz
    daisysp::Overdrive daisy_od;
    daisysp::Svf tone_filter;
    daisysp::Wavefolder wavefolder;
} Distortion;

void dist_init(Distortion *d, float drive, float tone, int mode);
float dist_process(Distortion *d, float x);

// ---- Chorus (DaisySP) ----
typedef struct {
    daisysp::Chorus daisy_chorus;
    float mix;
    int initialized;
} Chorus;

void chorus_init(Chorus *c, float rate, float depth, float mix);
void chorus_free(Chorus *c);
float chorus_process(Chorus *c, float x);

// ---- Phaser (DaisySP) ----
typedef struct {
    daisysp::Phaser daisy_phaser;
    float mix;
    int initialized;
} Phaser;

void phaser_init(Phaser *p, float rate, float depth, float mix);
float phaser_process(Phaser *p, float x);

// ---- Tremolo (DaisySP) ----
typedef struct {
    daisysp::Tremolo daisy_tremolo;
    float mix;
    int initialized;
} TremoloFx;

void tremolo_init(TremoloFx *t, float rate, float depth, float mix);
float tremolo_process(TremoloFx *t, float x);

// ---- Delay ----
typedef struct {
    float *buffer;
    int buf_size;
    int write_pos;
    float delay_ms;
    float feedback;
    float mix;
} Delay;

void delay_init(Delay *dl, float delay_ms, float feedback, float mix);
void delay_free(Delay *dl);

static inline float delay_process(Delay *dl, float x) {
    int delay_samples = (int)(dl->delay_ms * 0.001f * FX_SR);
    if (delay_samples >= dl->buf_size) delay_samples = dl->buf_size - 1;
    if (delay_samples < 1) return x;
    int read_pos = dl->write_pos - delay_samples;
    if (read_pos < 0) read_pos += dl->buf_size;
    float delayed = dl->buffer[read_pos];
    dl->buffer[dl->write_pos] = x + delayed * dl->feedback;
    dl->write_pos = (dl->write_pos + 1) % dl->buf_size;
    return x + delayed * dl->mix;
}

// ---- Reverb (Schroeder: 4 comb + 2 allpass) ----
typedef struct {
    float *comb_buf[4];
    int comb_size[4];
    int comb_pos[4];
    float comb_feedback[4];
    float ap_x1[2], ap_y1[2];
    float ap_feedback[2];
    float mix;
} Reverb;

void reverb_init(Reverb *rv, float room_size, float damping, float mix);
void reverb_free(Reverb *rv);

static inline float allpass_process(float *x1, float *y1, float fb, float x) {
    float y = -fb * x + *x1;
    *x1 = x + fb * y;
    *y1 = y;
    return y;
}

static inline float reverb_process(Reverb *rv, float x) {
    float comb_sum = 0;
    for (int i = 0; i < 4; i++) {
        int read = (rv->comb_pos[i] - 1 + rv->comb_size[i]) % rv->comb_size[i];
        float delayed = rv->comb_buf[i][read];
        rv->comb_buf[i][rv->comb_pos[i]] = x + delayed * rv->comb_feedback[i];
        rv->comb_pos[i] = (rv->comb_pos[i] + 1) % rv->comb_size[i];
        comb_sum += delayed;
    }
    comb_sum *= 0.25f;
    float y = allpass_process(&rv->ap_x1[0], &rv->ap_y1[0], rv->ap_feedback[0], comb_sum);
    y = allpass_process(&rv->ap_x1[1], &rv->ap_y1[1], rv->ap_feedback[1], y);
    return x * (1.0f - rv->mix) + y * rv->mix;
}

// ---- Shimmer Reverb ----
typedef struct {
    Reverb base_reverb;
    float *pitch_buf;
    int pitch_buf_size;
    int pitch_pos;
    float shimmer_mix;
} ShimmerReverb;

void shimmer_init(ShimmerReverb *sr, float room_size, float damping, float mix, float shimmer_octave);
void shimmer_free(ShimmerReverb *sr);
float shimmer_process(ShimmerReverb *sr, float x);

// ---- Compressor ----
typedef struct {
    float threshold;
    float ratio;
    float attack;
    float release;
    float envelope;
    float makeup_gain;
} Compressor;

void comp_init(Compressor *c, float threshold, float ratio, float attack_ms, float release_ms);
float comp_process(Compressor *c, float x);

// ---- Master FX Chain ----
typedef struct {
    NoiseGate gate;
    Compressor comp;
    Equalizer eq;
    Distortion dist;
    Chorus chorus;
    Phaser phaser;
    TremoloFx tremolo;
    Delay delay;
    Reverb reverb;
    ShimmerReverb shimmer;

    float input_gain;
    float output_vol;
    int bypass;

    int gate_on;
    int comp_on;
    int eq_on;
    int dist_on;
    int chorus_on;
    int phaser_on;
    int tremolo_on;
    int delay_on;
    int reverb_on;
    int shimmer_on;

    // DC blocker
    float hp_x1, hp_y1;

    // Monitoring
    volatile int64_t in_frames;
    volatile int64_t out_frames;
    volatile int xruns;
    volatile float peak_in;
    volatile float peak_out;
    volatile int running;
} FXChain;

void fxchain_init(FXChain *fx);
void fxchain_free(FXChain *fx);
float fxchain_process(FXChain *fx, float x);

static inline float soft_clip(float x) {
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    return tanhf(x);
}
