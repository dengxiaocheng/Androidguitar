#pragma once

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define LOOPER_MAX_LAYERS 8
#define LOOPER_MAX_FRAMES (48000 * 60 * 5) // 5 minutes max at 48kHz

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOOPER_IDLE = 0,
    LOOPER_RECORDING,
    LOOPER_PLAYING,
    LOOPER_OVERDUBBING
} LooperState;

typedef struct {
    float *buffer;
    int length;         // length in frames
    float volume;
    int muted;
} LooperLayer;

typedef struct {
    LooperLayer layers[LOOPER_MAX_LAYERS];
    int layer_count;
    int active_layer;

    LooperState state;
    int play_pos;       // current playback position in frames
    int loop_length;    // total loop length in frames
    int sample_rate;

    // Monitoring
    float peak;
    int64_t frames_processed;
} Looper;

void looper_init(Looper *l, int sample_rate);
void looper_free(Looper *l);

// Start recording a new layer
void looper_record(Looper *l);

// Stop recording and start looping
void looper_stop(Looper *l);

// Start overdubbing on top of existing loop
void looper_overdub(Looper *l);

// Undo last layer
void looper_undo(Looper *l);

// Process one frame: returns the mixed output
float looper_process(Looper *l, float input);

// Layer control
void looper_set_layer_volume(Looper *l, int layer, float volume);
void looper_set_layer_mute(Looper *l, int layer, int muted);
int looper_get_layer_count(Looper *l);
LooperState looper_get_state(Looper *l);

#ifdef __cplusplus
}
#endif
