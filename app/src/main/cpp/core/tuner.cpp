#include "tuner.h"
#include <math.h>
#include <string.h>

static const float NOTE_FREQS[] = {
    261.63f, 277.18f, 293.66f, 311.13f, 329.63f, 349.23f,
    369.99f, 392.00f, 415.30f, 440.00f, 466.16f, 493.88f
};

void tuner_init(Tuner *t) {
    memset(t, 0, sizeof(Tuner));
    t->threshold = 0.15f;
    t->detected_note = -1;
}

void tuner_push_sample(Tuner *t, float sample) {
    t->audio_buf[t->buf_pos] = sample;
    t->buf_pos = (t->buf_pos + 1) % TUNER_BUF_SIZE;
    if (t->buf_fill < TUNER_BUF_SIZE) t->buf_fill++;
}

int tuner_detect(Tuner *t) {
    if (t->buf_fill < TUNER_BUF_SIZE) return 0;

    int half = TUNER_BUF_SIZE / 2;

    // Step 1: Difference function
    for (int tau = 0; tau < half; tau++) {
        float sum = 0;
        for (int i = 0; i < half; i++) {
            int idx1 = (t->buf_pos - TUNER_BUF_SIZE + i + TUNER_BUF_SIZE) % TUNER_BUF_SIZE;
            int idx2 = (t->buf_pos - TUNER_BUF_SIZE + i + tau + TUNER_BUF_SIZE) % TUNER_BUF_SIZE;
            float d = t->audio_buf[idx1] - t->audio_buf[idx2];
            sum += d * d;
        }
        t->yin_buffer[tau] = sum;
    }

    // Step 2: Cumulative mean normalized difference
    t->yin_buffer[0] = 1.0f;
    float running_sum = 0;
    for (int tau = 1; tau < half; tau++) {
        running_sum += t->yin_buffer[tau];
        if (running_sum > 0.0001f)
            t->yin_buffer[tau] = t->yin_buffer[tau] * tau / running_sum;
        else
            t->yin_buffer[tau] = 1.0f;
    }

    // Step 3: Absolute threshold
    int tau_est = -1;
    for (int tau = 2; tau < half; tau++) {
        if (t->yin_buffer[tau] < t->threshold) {
            while (tau + 1 < half && t->yin_buffer[tau + 1] < t->yin_buffer[tau])
                tau++;
            tau_est = tau;
            break;
        }
    }

    if (tau_est == -1) {
        t->detected_freq = 0;
        t->clarity = 0;
        t->detected_note = -1;
        t->cents_off = 0;
        return 0;
    }

    // Step 4: Parabolic interpolation
    float s0 = (tau_est > 0) ? t->yin_buffer[tau_est - 1] : 1.0f;
    float s1 = t->yin_buffer[tau_est];
    float s2 = (tau_est + 1 < half) ? t->yin_buffer[tau_est + 1] : 1.0f;
    float denom = 2.0f * (s0 - 2.0f * s1 + s2);
    float shift = 0;
    if (fabsf(denom) > 0.0001f)
        shift = (s0 - s2) / denom;
    if (isnan(shift) || isinf(shift)) shift = 0;
    float refined_tau = tau_est + shift;

    t->detected_freq = (float)TUNER_SR / refined_tau;
    t->clarity = 1.0f - s1;

    if (t->detected_freq < 60.0f || t->detected_freq > 1500.0f) {
        t->detected_note = -1;
        t->cents_off = 0;
        return 0;
    }

    // Find closest note
    float min_dist = 999999.0f;
    int best_note = 0;
    int best_octave = 4;

    for (int oct = 2; oct <= 6; oct++) {
        for (int n = 0; n < 12; n++) {
            float ref = NOTE_FREQS[n] * powf(2.0f, oct - 4);
            float dist = fabsf(t->detected_freq - ref);
            if (dist < min_dist) {
                min_dist = dist;
                best_note = n;
                best_octave = oct;
            }
        }
    }

    t->detected_note = best_note;
    float ref_freq = NOTE_FREQS[best_note] * powf(2.0f, best_octave - 4);
    t->cents_off = 1200.0f * log2f(t->detected_freq / ref_freq);
    return 1;
}
