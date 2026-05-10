#pragma once
#include <math.h>
#include <string.h>

// Real-time audio spectrum analyzer
// References:
//   - bewantbe/audio-analyzer-for-android (Apache 2.0) - STFT.java
//     https://github.com/bewantbe/audio-analyzer-for-android
//   - borisRadonic/AudioAnalyzer (MIT) - four.cpp, window.cpp
//     https://github.com/borisRadonic/AudioAnalyzer
//   - KissFFT (BSD) - berndporr/kiss-fft
//     https://github.com/berndporr/kiss-fft
//
// Key techniques from references:
//   - STFT with 50% overlap (bewantbe STFT.java)
//   - Window energy compensation factor (bewantbe)
//   - A-weighting for noise measurement (bewantbe)
//   - Proper FFT magnitude scaling: 2*2/(N*N) (bewantbe fftToAmp)
//   - Quadratic interpolation for peak frequency (bewantbe calculatePeak)
//   - dBFS calculation: 20*log10(mag/fullScale) (borisRadonic)
//   - Hann window (both projects agree this is good for audio)

#define ANALYZER_FFT_SIZE 1024
#define ANALYZER_NUM_BANDS 8

namespace analyzer {

// Frequency bands for guitar analysis (Hz)
static const float band_bounds[ANALYZER_NUM_BANDS][2] = {
    {0,    100},   // Sub-bass, DC
    {100,  200},   // Low fundamentals (E2=82Hz, A2=110Hz)
    {200,  500},   // Guitar body (D3-G3)
    {500,  1000},  // Mid fundamentals
    {1000, 2000},  // Upper mids
    {2000, 4000},  // Presence
    {4000, 8000},  // Brightness
    {8000, 24000}, // Air, hiss
};

struct Stats {
    float rms;              // Time-domain RMS (0-1), normalized for sine
    float peak;             // Peak amplitude (0-1)
    float dc_offset;        // Mean value
    int clip_count;         // Samples exceeding 0.95
    float zcr;              // Zero crossing rate

    float band_energy[ANALYZER_NUM_BANDS];  // Per-band RMS (0-1)
    float band_db[ANALYZER_NUM_BANDS];      // Per-band dBFS

    float peak_freq;        // Dominant frequency (Hz), quadratic interpolated
    float peak_db;          // Peak frequency amplitude (dBFS)
    float thd_estimate;     // THD: harmonic energy / fundamental
    float noise_floor_db;   // Quietest band in dBFS
    float snr_db;           // Signal-to-noise ratio (dB)
    float dynamic_range_db; // Peak - noise floor (dB)
};

class AudioAnalyzer {
public:
    void init(int sample_rate) {
        sr = sample_rate;
        buf_pos = 0;
        frame_count = 0;
        hop_len = ANALYZER_FFT_SIZE / 2;  // 50% overlap (bewantbe default)

        // Hann window (from bewantbe STFT.java, borisRadonic window.cpp)
        wnd_energy = 0;
        for (int i = 0; i < ANALYZER_FFT_SIZE; i++) {
            // Hann: 0.5 * (1 - cos(2*pi*i/(N-1)))
            wnd[i] = 0.5f * (1.0f - cosf(2.0f * 3.14159265f * i / (ANALYZER_FFT_SIZE - 1)));
            wnd_energy += wnd[i] * wnd[i];
        }
        wnd_energy_factor = (float)ANALYZER_FFT_SIZE / wnd_energy;

        // Pre-compute band bin ranges
        for (int b = 0; b < ANALYZER_NUM_BANDS; b++) {
            band_lo_bin[b] = (int)(band_bounds[b][0] * ANALYZER_FFT_SIZE / sample_rate);
            band_hi_bin[b] = (int)(band_bounds[b][1] * ANALYZER_FFT_SIZE / sample_rate);
            if (band_hi_bin[b] > ANALYZER_FFT_SIZE / 2) band_hi_bin[b] = ANALYZER_FFT_SIZE / 2;
            if (band_lo_bin[b] < 0) band_lo_bin[b] = 0;
        }

        // A-weighting factors (from bewantbe STFT.java initDBAFactor)
        // Standard IEC 61672-1 A-weighting curve
        for (int i = 0; i <= ANALYZER_FFT_SIZE / 2; i++) {
            float f = (float)i / ANALYZER_FFT_SIZE * sample_rate;
            float f2 = f * f;
            float r = 12200.0f*12200.0f * f2*f2 /
                ((f2 + 20.6f*20.6f) *
                 sqrtf((f2 + 107.7f*107.7f) * (f2 + 737.9f*737.9f)) *
                 (f2 + 12200.0f*12200.0f));
            a_weight[i] = r * r * 1.58489319246111f;  // 10^(1/5)
        }

        memset(fft_buf, 0, sizeof(fft_buf));
        memset(&stats, 0, sizeof(stats));
        stat_cum_rms = 0;
        stat_rms_count = 0;
        stat_peak_val = 0;
        stat_clips = 0;
        stat_zcr_count = 0;
        stat_sum = 0;
        prev_sample = 0;
    }

    // Push one sample. Returns true when a new FFT frame is ready.
    bool push(float sample) {
        // Accumulate time-domain stats
        stat_sum += sample;
        stat_cum_rms += (double)sample * sample;
        stat_rms_count++;
        float abs_s = fabsf(sample);
        if (abs_s > stat_peak_val) stat_peak_val = abs_s;
        if (abs_s > 0.95f) stat_clips++;
        if (stat_rms_count > 1) {
            if ((prev_sample >= 0 && sample < 0) || (prev_sample < 0 && sample >= 0))
                stat_zcr_count++;
        }
        prev_sample = sample;

        // Fill FFT buffer
        fft_buf[buf_pos] = sample;
        buf_pos++;

        if (buf_pos >= ANALYZER_FFT_SIZE) {
            compute_frame();
            // 50% overlap: shift by hop_len (bewantbe STFT.java)
            memmove(fft_buf, fft_buf + hop_len,
                    (ANALYZER_FFT_SIZE - hop_len) * sizeof(float));
            buf_pos = ANALYZER_FFT_SIZE - hop_len;
            return true;
        }
        return false;
    }

    const Stats& get_stats() const { return stats; }

private:
    void compute_frame() {
        int N = ANALYZER_FFT_SIZE;

        // Apply window
        float windowed[ANALYZER_FFT_SIZE];
        for (int i = 0; i < N; i++) windowed[i] = fft_buf[i] * wnd[i];

        // FFT (radix-2 Cooley-Tukey, same algorithm as KissFFT)
        float real[ANALYZER_FFT_SIZE], imag[ANALYZER_FFT_SIZE];
        memcpy(real, windowed, N * sizeof(float));
        memset(imag, 0, N * sizeof(float));

        // Bit-reversal permutation
        for (int i = 1, j = 0; i < N; i++) {
            int bit = N >> 1;
            while (j & bit) { j ^= bit; bit >>= 1; }
            j ^= bit;
            if (i < j) {
                float t = real[i]; real[i] = real[j]; real[j] = t;
                t = imag[i]; imag[i] = imag[j]; imag[j] = t;
            }
        }

        // Butterfly (Danielson-Lanczos, from Numerical Recipes / KissFFT)
        for (int len = 2; len <= N; len <<= 1) {
            float ang = -2.0f * 3.14159265f / len;
            float w_re = cosf(ang), w_im = sinf(ang);
            for (int i = 0; i < N; i += len) {
                float cur_re = 1.0f, cur_im = 0.0f;
                for (int j = 0; j < len / 2; j++) {
                    float u_re = real[i+j], u_im = imag[i+j];
                    float v_re = real[i+j+len/2]*cur_re - imag[i+j+len/2]*cur_im;
                    float v_im = real[i+j+len/2]*cur_im + imag[i+j+len/2]*cur_re;
                    real[i+j] = u_re + v_re;
                    imag[i+j] = u_im + v_im;
                    real[i+j+len/2] = u_re - v_re;
                    imag[i+j+len/2] = u_im - v_im;
                    float nr = cur_re*w_re - cur_im*w_im;
                    cur_im = cur_re*w_im + cur_im*w_re;
                    cur_re = nr;
                }
            }
        }

        // Power spectrum with proper scaling (bewantbe fftToAmp)
        // scaler = 2*2/(N*N), *2 for positive+negative freq
        float power[N/2 + 1];
        float scaler = 4.0f / ((float)N * (float)N);
        power[0] = real[0]*real[0] * scaler / 4.0f;  // DC
        int j;
        for (j = 1; j < N - 1; j += 2) {
            int bin = (j + 1) / 2;
            power[bin] = (real[j]*real[j] + real[j+1]*real[j+1]) * scaler;
        }
        if (j <= N) power[N/2] = real[N-1]*real[N-1] * scaler / 4.0f;  // Nyquist

        // Per-band energy (RMS of power in each band)
        for (int b = 0; b < ANALYZER_NUM_BANDS; b++) {
            float energy = 0;
            int count = 0;
            for (int k = band_lo_bin[b]; k < band_hi_bin[b]; k++) {
                energy += power[k];
                count++;
            }
            stats.band_energy[b] = count > 0 ? sqrtf(energy / count) * wnd_energy_factor : 0;
            stats.band_db[b] = stats.band_energy[b] > 1e-10f ?
                10.0f * log10f(stats.band_energy[b]) : -100.0f;
        }

        // Peak frequency with quadratic interpolation (bewantbe calculatePeak)
        float max_power = -1e30f;
        int max_bin = 1;
        for (int k = 1; k < N/2; k++) {  // skip DC
            if (power[k] > max_power) {
                max_power = power[k];
                max_bin = k;
            }
        }
        stats.peak_db = max_power > 1e-10f ? 10.0f * log10f(max_power * wnd_energy_factor) : -100.0f;
        stats.peak_freq = (float)max_bin * sr / N;

        // Quadratic interpolation for sub-bin accuracy (bewantbe)
        if (max_bin > 1 && max_bin < N/2 - 1) {
            float x1 = 10.0f * log10f(power[max_bin-1] * wnd_energy_factor + 1e-30f);
            float x2 = 10.0f * log10f(power[max_bin] * wnd_energy_factor + 1e-30f);
            float x3 = 10.0f * log10f(power[max_bin+1] * wnd_energy_factor + 1e-30f);
            float a = (x3 + x1) / 2.0f - x2;
            float b = (x3 - x1) / 2.0f;
            if (a < 0) {
                float xPeak = -b / (2.0f * a);
                if (fabsf(xPeak) < 1.0f) {
                    stats.peak_freq += xPeak * sr / N;
                    stats.peak_db = (4.0f*a*x2 - b*b) / (4.0f*a);
                }
            }
        }

        // Time-domain stats (bewantbe getRMS)
        if (stat_rms_count > 0) {
            stats.rms = sqrtf((float)(stat_cum_rms / stat_rms_count) * 2.0f);  // *2 normalize for sine
            stats.peak = stat_peak_val;
            stats.dc_offset = (float)(stat_sum / stat_rms_count);
        }
        stats.clip_count = stat_clips;
        stats.zcr = stat_rms_count > 0 ? (float)stat_zcr_count / stat_rms_count : 0;

        // Noise floor = quietest band dBFS
        float min_db = 0;
        for (int b = 0; b < ANALYZER_NUM_BANDS; b++) {
            if (stats.band_db[b] < min_db) min_db = stats.band_db[b];
        }
        stats.noise_floor_db = min_db;

        // SNR: peak band - noise floor (borisRadonic fftMagdB approach)
        stats.snr_db = stats.peak_db - min_db;

        // Dynamic range
        float peak_db_time = stats.peak > 1e-10f ? 20.0f * log10f(stats.peak) : -100.0f;
        stats.dynamic_range_db = peak_db_time - min_db;

        // THD estimate: harmonics / fundamental
        float fundamental = 0;
        for (int b = 2; b <= 3; b++) fundamental += stats.band_energy[b];
        float harmonics = 0;
        for (int b = 4; b <= 7; b++) harmonics += stats.band_energy[b];
        stats.thd_estimate = fundamental > 0.001f ? harmonics / fundamental : 0;

        // Reset accumulators
        stat_cum_rms = 0;
        stat_rms_count = 0;
        stat_peak_val = 0;
        stat_clips = 0;
        stat_zcr_count = 0;
        stat_sum = 0;
    }

    int sr;
    float fft_buf[ANALYZER_FFT_SIZE];
    float wnd[ANALYZER_FFT_SIZE];
    float wnd_energy;
    float wnd_energy_factor;
    float a_weight[ANALYZER_FFT_SIZE / 2 + 1];
    int band_lo_bin[ANALYZER_NUM_BANDS];
    int band_hi_bin[ANALYZER_NUM_BANDS];
    int buf_pos;
    int hop_len;
    int frame_count;
    Stats stats;

    double stat_cum_rms;
    float stat_sum, stat_peak_val;
    int stat_clips, stat_zcr_count, stat_rms_count;
    float prev_sample;
};

} // namespace analyzer
