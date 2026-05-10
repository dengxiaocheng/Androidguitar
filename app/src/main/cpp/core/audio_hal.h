#pragma once

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Audio callback: process numFrames of audio
// in = interleaved input samples (stereo), out = interleaved output samples (stereo)
typedef void (*AudioCallback)(const float *in, float *out, int32_t numFrames, void *userdata);

typedef struct {
    char name[128];
    int id;
} AudioDeviceInfo;

typedef struct AudioHAL AudioHAL;

struct AudioHAL {
    // Lifecycle
    int (*init)(int sample_rate, int buffer_size, AudioCallback cb, void *userdata);
    int (*start)(void);
    int (*stop)(void);
    void (*destroy)(void);

    // Device enumeration
    int (*get_input_devices)(AudioDeviceInfo *devices, int max_count);
    int (*get_output_devices)(AudioDeviceInfo *devices, int max_count);

    // Device selection (call before start)
    int (*set_input_device)(int device_id);
    int (*set_output_device)(int device_id);

    // Info
    int (*get_sample_rate)(void);
    int (*get_buffer_size)(void);
    int (*get_xrun_count)(void);
};

// Create HAL for current platform
AudioHAL *audio_hal_create(void);

#ifdef __cplusplus
}
#endif
