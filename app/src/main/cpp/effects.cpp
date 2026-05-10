#include "effects.h"

// ---- Noise Gate ----
void gate_init(NoiseGate *g, float threshold, float attack_ms, float release_ms) {
    g->threshold = threshold;
    g->attack = expf(-1.0f / (attack_ms * 0.001f * FX_SR));
    g->release = expf(-1.0f / (release_ms * 0.001f * FX_SR));
    g->envelope = 0;
    g->gated = 0;
}

// ---- EQ (series biquad peaking - standard parametric EQ) ----
static void biquad_calc(BiquadBand *b, float freq, float gain_db) {
    b->freq = freq;
    b->gain_db = gain_db;
    float A = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * FX_PI * freq / FX_SR;
    float cosw = cosf(w0);
    float sinw = sinf(w0);
    float Q = 1.2f;
    float alpha = sinw / (2.0f * Q);
    float norm = 1.0f + alpha / A;
    b->b0 = (1.0f + alpha * A) / norm;
    b->b1 = (-2.0f * cosw) / norm;
    b->b2 = (1.0f - alpha * A) / norm;
    b->a1 = (-2.0f * cosw) / norm;
    b->a2 = (1.0f - alpha / A) / norm;
    b->x1 = b->x2 = b->y1 = b->y2 = 0;
}

void eq_init(Equalizer *eq, float low_db, float mid_db, float high_db) {
    biquad_calc(&eq->low, 200.0f, low_db);
    biquad_calc(&eq->mid, 800.0f, mid_db);
    biquad_calc(&eq->high, 3000.0f, high_db);
}

// ---- Distortion (DaisySP Overdrive + Wavefolder + Svf tone) ----
void dist_init(Distortion *d, float drive, float tone, int mode) {
    d->drive = drive;
    d->tone = tone;
    d->mix = 1.0f;
    d->mode = mode;

    d->daisy_od.Init();
    d->tone_filter.Init(FX_SR);
    d->wavefolder.Init();

    float daisy_drive = (drive - 1.0f) / 19.0f;
    if (daisy_drive > 1.0f) daisy_drive = 1.0f;
    if (daisy_drive < 0.0f) daisy_drive = 0.0f;
    if (mode == 2) daisy_drive = daisy_drive * 0.5f + 0.5f;
    d->daisy_od.SetDrive(daisy_drive);

    float cutoff = 600.0f + tone * (FX_SR * 0.4f - 600.0f);
    d->tone_filter.SetFreq(cutoff);
    d->tone_filter.SetRes(0.3f);

    d->wavefolder.SetGain(drive * 0.5f);
    d->wavefolder.SetOffset(0.0f);
}

float dist_process(Distortion *d, float x) {
    if (d->mode == 0) return x;
    float dry = x;
    float wet;

    switch (d->mode) {
        case 1: // Overdrive
            wet = d->daisy_od.Process(x);
            break;
        case 2: // Distortion - overdrive + tone filter
            wet = d->daisy_od.Process(x);
            d->tone_filter.Process(wet);
            wet = d->tone_filter.Low();
            break;
        case 3: // Fuzz - wavefolder
            wet = d->wavefolder.Process(x * d->drive * 0.3f);
            break;
        default:
            wet = x;
            break;
    }

    return dry * (1.0f - d->mix) + wet * d->mix;
}

// ---- Chorus (DaisySP) ----
void chorus_init(Chorus *c, float rate, float depth, float mix) {
    c->daisy_chorus.Init(FX_SR);
    c->daisy_chorus.SetLfoFreq(rate, rate * 1.1f);
    c->daisy_chorus.SetLfoDepth(depth, depth);
    c->daisy_chorus.SetDelayMs(5.0f, 7.0f);
    c->daisy_chorus.SetFeedback(0.2f, 0.3f);
    c->mix = mix;
    c->initialized = 1;
}

void chorus_free(Chorus *c) { c->initialized = 0; }

float chorus_process(Chorus *c, float x) {
    if (!c->initialized) return x;
    float wet = c->daisy_chorus.Process(x);
    return x * (1.0f - c->mix) + wet * c->mix;
}

// ---- Phaser (DaisySP) ----
void phaser_init(Phaser *p, float rate, float depth, float mix) {
    p->daisy_phaser.Init(FX_SR);
    p->daisy_phaser.SetLfoFreq(rate);
    p->daisy_phaser.SetLfoDepth(depth);
    p->daisy_phaser.SetFeedback(0.5f);
    p->daisy_phaser.SetFreq(800.0f);
    p->mix = mix;
    p->initialized = 1;
}

float phaser_process(Phaser *p, float x) {
    if (!p->initialized) return x;
    float wet = p->daisy_phaser.Process(x);
    return x * (1.0f - p->mix) + wet * p->mix;
}

// ---- Tremolo (DaisySP) ----
void tremolo_init(TremoloFx *t, float rate, float depth, float mix) {
    t->daisy_tremolo.Init(FX_SR);
    t->daisy_tremolo.SetFreq(rate);
    t->daisy_tremolo.SetDepth(depth);
    t->mix = mix;
    t->initialized = 1;
}

float tremolo_process(TremoloFx *t, float x) {
    if (!t->initialized) return x;
    float wet = t->daisy_tremolo.Process(x);
    return x * (1.0f - t->mix) + wet * t->mix;
}

// ---- Delay ----
void delay_init(Delay *dl, float delay_ms, float feedback, float mix) {
    dl->buf_size = FX_SR;
    dl->buffer = (float *)calloc(dl->buf_size, sizeof(float));
    dl->write_pos = 0;
    dl->delay_ms = delay_ms;
    dl->feedback = feedback;
    dl->mix = mix;
}

void delay_free(Delay *dl) {
    if (dl->buffer) { free(dl->buffer); dl->buffer = nullptr; }
}

// ---- Reverb ----
void reverb_init(Reverb *rv, float room_size, float damping, float mix) {
    int comb_delays[] = {1557, 1617, 1491, 1422};
    float comb_fb_base[] = {0.84f, 0.83f, 0.82f, 0.81f};
    for (int i = 0; i < 4; i++) {
        int len = (int)(comb_delays[i] * room_size);
        if (len < 64) len = 64;
        if (len > FX_SR) len = FX_SR;
        rv->comb_buf[i] = (float *)calloc(len, sizeof(float));
        rv->comb_size[i] = len;
        rv->comb_pos[i] = 0;
        rv->comb_feedback[i] = comb_fb_base[i] * (1.0f - damping * 0.5f);
    }
    rv->ap_feedback[0] = 0.5f;
    rv->ap_feedback[1] = 0.5f;
    rv->ap_x1[0] = rv->ap_y1[0] = 0;
    rv->ap_x1[1] = rv->ap_y1[1] = 0;
    rv->mix = mix;
}

void reverb_free(Reverb *rv) {
    for (int i = 0; i < 4; i++) {
        if (rv->comb_buf[i]) { free(rv->comb_buf[i]); rv->comb_buf[i] = nullptr; }
    }
}

// ---- Shimmer Reverb ----
void shimmer_init(ShimmerReverb *sr, float room_size, float damping, float mix, float shimmer_mix) {
    reverb_init(&sr->base_reverb, room_size * 1.5f, damping * 0.7f, mix);
    sr->pitch_buf_size = 2048;
    sr->pitch_buf = (float *)calloc(sr->pitch_buf_size, sizeof(float));
    sr->pitch_pos = 0;
    sr->shimmer_mix = shimmer_mix;
}

void shimmer_free(ShimmerReverb *sr) {
    reverb_free(&sr->base_reverb);
    if (sr->pitch_buf) { free(sr->pitch_buf); sr->pitch_buf = nullptr; }
}

float shimmer_process(ShimmerReverb *sr, float x) {
    float reverb_out = reverb_process(&sr->base_reverb, x);
    sr->pitch_buf[sr->pitch_pos % sr->pitch_buf_size] = reverb_out;
    sr->pitch_pos++;
    int half_pos = (sr->pitch_pos / 2) % sr->pitch_buf_size;
    float shifted = sr->pitch_buf[half_pos];
    return reverb_out * (1.0f - sr->shimmer_mix) + shifted * sr->shimmer_mix;
}

// ---- Compressor ----
void comp_init(Compressor *c, float threshold, float ratio, float attack_ms, float release_ms) {
    c->threshold = threshold;
    c->ratio = ratio;
    c->attack = expf(-1.0f / (attack_ms * 0.001f * FX_SR));
    c->release = expf(-1.0f / (release_ms * 0.001f * FX_SR));
    c->envelope = 0;
    c->makeup_gain = 1.0f;
}

float comp_process(Compressor *c, float x) {
    float abs_x = fabsf(x);
    if (abs_x > c->envelope)
        c->envelope = c->attack * c->envelope + (1.0f - c->attack) * abs_x;
    else
        c->envelope = c->release * c->envelope + (1.0f - c->release) * abs_x;

    if (c->envelope > c->threshold) {
        float gain_db = (c->threshold - c->envelope) * (1.0f - 1.0f / c->ratio);
        float gain = powf(10.0f, gain_db / 20.0f);
        return x * gain * c->makeup_gain;
    }
    return x * c->makeup_gain;
}

// ---- FX Chain ----
static inline float dc_block(FXChain *fx, float x) {
    float y = 0.995f * fx->hp_y1 + 0.995f * (x - fx->hp_x1);
    fx->hp_x1 = x;
    fx->hp_y1 = y;
    return y;
}

void fxchain_init(FXChain *fx) {
    memset(fx, 0, sizeof(FXChain));
    fx->input_gain = 5.0f;
    fx->output_vol = 1.0f;
    fx->running = 0;

    // Minimal defaults: just overdrive
    fx->gate_on = 0;
    fx->comp_on = 0;
    fx->eq_on = 0;
    fx->dist_on = 1;
    fx->chorus_on = 0;
    fx->phaser_on = 0;
    fx->tremolo_on = 0;
    fx->delay_on = 0;
    fx->reverb_on = 0;
    fx->shimmer_on = 0;

    gate_init(&fx->gate, 0.015f, 1.0f, 50.0f);
    comp_init(&fx->comp, 0.3f, 4.0f, 5.0f, 50.0f);
    eq_init(&fx->eq, 0, 0, 0);  // flat EQ
    dist_init(&fx->dist, 5.0f, 0.5f, 1);  // mild overdrive
    chorus_init(&fx->chorus, 1.5f, 0.8f, 0.5f);
    phaser_init(&fx->phaser, 0.5f, 0.5f, 0.5f);
    tremolo_init(&fx->tremolo, 2.0f, 0.5f, 0.5f);
    delay_init(&fx->delay, 300.0f, 0.4f, 0.3f);
    reverb_init(&fx->reverb, 0.5f, 0.3f, 0.3f);
    shimmer_init(&fx->shimmer, 0.7f, 0.2f, 0.3f, 0.5f);
}

void fxchain_free(FXChain *fx) {
    delay_free(&fx->delay);
    reverb_free(&fx->reverb);
    shimmer_free(&fx->shimmer);
    chorus_free(&fx->chorus);
}

float fxchain_process(FXChain *fx, float x) {
    x *= fx->input_gain;
    x = dc_block(fx, x);

    float a = fabsf(x);
    if (a > fx->peak_in) fx->peak_in = a;

    if (fx->bypass) return soft_clip(x * fx->output_vol);

    if (fx->gate_on) x = gate_process(&fx->gate, x);
    if (fx->comp_on) x = comp_process(&fx->comp, x);
    if (fx->eq_on) x = eq_process(&fx->eq, x);
    if (fx->dist_on) x = dist_process(&fx->dist, x);
    if (fx->chorus_on) x = chorus_process(&fx->chorus, x);
    if (fx->phaser_on) x = phaser_process(&fx->phaser, x);
    if (fx->tremolo_on) x = tremolo_process(&fx->tremolo, x);
    if (fx->delay_on) x = delay_process(&fx->delay, x);
    if (fx->reverb_on) x = reverb_process(&fx->reverb, x);
    if (fx->shimmer_on) x = shimmer_process(&fx->shimmer, x);

    x *= fx->output_vol;
    x = soft_clip(x);
    a = fabsf(x);
    if (a > fx->peak_out) fx->peak_out = a;
    return x;
}
