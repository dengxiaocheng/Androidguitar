#pragma once

#include <math.h>
#include <string.h>

#define TUNER_BUF_SIZE 2048
#define TUNER_SR 48000

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float yin_buffer[TUNER_BUF_SIZE / 2];
    float audio_buf[TUNER_BUF_SIZE];
    int buf_pos;
    int buf_fill;
    float threshold;
    float detected_freq;
    float clarity;
    int detected_note;  // 0=C, 1=C#, ... 11=B, -1=none
    float cents_off;
} Tuner;

void tuner_init(Tuner *t);
void tuner_push_sample(Tuner *t, float sample);
int tuner_detect(Tuner *t);  // returns 1 if pitch found

static const char *TUNER_NOTES[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

#ifdef __cplusplus
}
#endif
