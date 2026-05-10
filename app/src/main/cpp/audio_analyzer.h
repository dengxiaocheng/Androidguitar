#pragma once
#include <math.h>
#include <string.h>

// Simple radix-2 FFT (Cooley-Tukey)
// For real-time audio spectrum analysis on device

#define ANALYZER_FFT_SIZE 1024
#define ANALYZER_NUM_BANDS 8

namespace analyzer {

// Frequency bands for guitar analysis
// Band 0: 0-100Hz   (sub-bass, DC, rumble)
// Band 1: 100-200Hz (low fundamentals)
// Band 2: 200-500Hz (guitar body)
// Band 3: 500-1kHz  (mid fundamentals)
// Band 4: 1k-2kHz   (upper mids)
// Band 5: 2k-4kHz   (presence)
// Band 6: 4k-8kHz   (brightness)
// Band 7: 8k-24kHz  (air, noise)

struct BandDef {
    float lo_hz;
    float hi_hz;
};

static const BandDef bands[ANALYZER_NUM_BANDS] = {
    {0,    100},
    {100,  200},
    {200,  500},
    {500,  1000},
    {1000, 2000},
    {2000, 4000},
    {4000, 8000},
    {8000, 24000},
};

struct Stats {
    // Waveform stats
    float rms;           // RMS level (0-1)
    float peak;          // Peak level (0-1)
    float dc_offset;     // DC offset
    int clip_count;      // Samples near ±1.0
    float zcr;           // Zero crossing rate (noise indicator)

    // Spectrum bands (dB-like, 0=silent, 1=full scale)
    float band_energy[ANALYZER_NUM_BANDS];

    // Derived metrics
    float thd_estimate;  // THD estimate (harmonic energy / fundamental)
    float noise_floor;   // Estimated noise floor (RMS of quietest band)
    float snr;           // Signal-to-noise ratio in dB
    float dynamic_range; // Peak - noise_floor in dB
};

class AudioAnalyzer {
public:
    void init(int sample_rate) {
        sr = sample_rate;
        memset(fft_buf, 0, sizeof(fft_buf));
        memset(window, 0, sizeof(window));
        buf_pos = 0;
        frame_count = 0;

        // Pre-compute Hann window
        for (int i = 0; i < ANALYZER_FFT_SIZE; i++) {
            window[i] = 0.5f * (1.0f - cosf(2.0f * 3.14159265f * i / (ANALYZER_FFT_SIZE - 1)));
        }

        // Pre-compute band bin ranges
        for (int b = 0; b < ANALYZER_NUM_BANDS; b++) {
            band_lo_bin[b] = (int)(bands[b].lo_hz * ANALYZER_FFT_SIZE / sample_rate);
            band_hi_bin[b] = (int)(bands[b].hi_hz * ANALYZER_FFT_SIZE / sample_rate);
            if (band_hi_bin[b] > ANALYZER_FFT_SIZE / 2) band_hi_bin[b] = ANALYZER_FFT_SIZE / 2;
            if (band_lo_bin[b] < 0) band_lo_bin[b] = 0;
        }

        memset(&stats, 0, sizeof(stats));
    }

    // Push a sample. Returns true when a new analysis frame is ready.
    bool push(float sample) {
        // Accumulate waveform stats
        stat_sum += sample;
        stat_sq_sum += sample * sample;
        stat_peak_val = fmaxf(stat_peak_val, fabsf(sample));
        if (fabsf(sample) > 0.95f) stat_clips++;
        if (stat_count > 0) {
            if ((prev_sample >= 0 && sample < 0) || (prev_sample < 0 && sample >= 0))
                stat_zcr_count++;
        }
        prev_sample = sample;
        stat_count++;

        // Fill FFT buffer with windowed samples
        fft_buf[buf_pos] = sample * window[buf_pos];
        buf_pos++;
        if (buf_pos >= ANALYZER_FFT_SIZE) {
            buf_pos = 0;
            compute_spectrum();
            compute_stats();
            frame_count++;
            return true;
        }
        return false;
    }

    const Stats& get_stats() const { return stats; }
    int get_frame_count() const { return frame_count; }

private:
    void compute_spectrum() {
        // In-place radix-2 FFT on fft_buf (real part), imag part in fft_imag
        float real[ANALYZER_FFT_SIZE];
        float imag[ANALYZER_FFT_SIZE];
        memcpy(real, fft_buf, ANALYZER_FFT_SIZE * sizeof(float));
        memset(imag, 0, ANALYZER_FFT_SIZE * sizeof(float));

        // Bit-reversal permutation
        int n = ANALYZER_FFT_SIZE;
        for (int i = 1, j = 0; i < n; i++) {
            int bit = n >> 1;
            while (j & bit) { j ^= bit; bit >>= 1; }
            j ^= bit;
            if (i < j) {
                float tmp = real[i]; real[i] = real[j]; real[j] = tmp;
                tmp = imag[i]; imag[i] = imag[j]; imag[j] = tmp;
            }
        }

        // Cooley-Tukey butterfly
        for (int len = 2; len <= n; len <<= 1) {
            float ang = 2.0f * 3.14159265f / len;
            float w_re = cosf(ang);
            float w_im = sinf(ang);
            for (int i = 0; i < n; i += len) {
                float cur_re = 1.0f, cur_im = 0.0f;
                for (int j = 0; j < len / 2; j++) {
                    float u_re = real[i + j], u_im = imag[i + j];
                    float v_re = real[i + j + len/2] * cur_re - imag[i + j + len/2] * cur_im;
                    float v_im = real[i + j + len/2] * cur_im + imag[i + j + len/2] * cur_re;
                    real[i + j] = u_re + v_re;
                    imag[i + j] = u_im + v_im;
                    real[i + j + len/2] = u_re - v_re;
                    imag[i + j + len/2] = u_im - v_im;
                    float new_re = cur_re * w_re - cur_im * w_im;
                    cur_im = cur_re * w_im + cur_im * w_re;
                    cur_re = new_re;
                }
            }
        }

        // Compute magnitude spectrum and band energies
        for (int b = 0; b < ANALYZER_NUM_BANDS; b++) {
            float energy = 0;
            int count = 0;
            for (int k = band_lo_bin[b]; k < band_hi_bin[b]; k++) {
                float mag = sqrtf(real[k] * real[k] + imag[k] * imag[k]) / ANALYZER_FFT_SIZE;
                energy += mag * mag;
                count++;
            }
            stats.band_energy[b] = count > 0 ? sqrtf(energy / count) : 0;
        }
    }

    void compute_stats() {
        int n = stat_count;
        if (n == 0) return;

        stats.rms = sqrtf(stat_sq_sum / n);
        stats.peak = stat_peak_val;
        stats.dc_offset = stat_sum / n;
        stats.clip_count = stat_clips;
        stats.zcr = (float)stat_zcr_count / n;

        // Find noise floor = RMS of quietest band
        float min_band = 1.0f;
        for (int b = 0; b < ANALYZER_NUM_BANDS; b++) {
            if (stats.band_energy[b] < min_band && stats.band_energy[b] > 0)
                min_band = stats.band_energy[b];
        }
        stats.noise_floor = min_band;

        // Find loudest band (fundamental)
        float max_band = 0;
        int max_band_idx = 0;
        for (int b = 0; b < ANALYZER_NUM_BANDS; b++) {
            if (stats.band_energy[b] > max_band) {
                max_band = stats.band_energy[b];
                max_band_idx = b;
            }
        }

        // SNR in dB
        if (max_band > 0 && min_band > 0)
            stats.snr = 20.0f * log10f(max_band / min_band);
        else
            stats.snr = 0;

        // Dynamic range
        if (stats.peak > 0 && min_band > 0)
            stats.dynamic_range = 20.0f * log10f(stats.peak / min_band);
        else
            stats.dynamic_range = 0;

        // THD estimate: harmonic bands energy / fundamental band energy
        // Fundamental is typically band 2-3 (200-1kHz for guitar)
        float fundamental = 0;
        for (int b = 2; b <= 3; b++) fundamental += stats.band_energy[b];
        fundamental *= 0.5f;

        float harmonics = 0;
        for (int b = 4; b <= 7; b++) harmonics += stats.band_energy[b];

        if (fundamental > 0.001f)
            stats.thd_estimate = harmonics / fundamental;
        else
            stats.thd_estimate = 0;

        // Reset accumulators
        stat_sum = stat_sq_sum = 0;
        stat_peak_val = 0;
        stat_clips = 0;
        stat_zcr_count = 0;
        stat_count = 0;
    }

    int sr;
    float fft_buf[ANALYZER_FFT_SIZE];
    float window[ANALYZER_FFT_SIZE];
    int band_lo_bin[ANALYZER_NUM_BANDS];
    int band_hi_bin[ANALYZER_NUM_BANDS];
    int buf_pos;
    int frame_count;
    Stats stats;

    // Running accumulators
    float stat_sum = 0, stat_sq_sum = 0, stat_peak_val = 0;
    int stat_clips = 0, stat_zcr_count = 0, stat_count = 0;
    float prev_sample = 0;
};

} // namespace analyzer
