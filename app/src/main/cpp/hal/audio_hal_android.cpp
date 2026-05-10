#include "core/audio_hal.h"
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "AudioHAL"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// Stub - engine.cpp handles AAudio directly. This HAL is for future abstraction.

static int hal_init(int sr, int buf, AudioCallback cb, void *ud) { return 0; }
static int hal_start(void) { return 0; }
static int hal_stop(void) { return 0; }
static void hal_destroy(void) {}

static int hal_get_input_devices(AudioDeviceInfo *d, int max) {
    if (max > 0) { strncpy(d[0].name, "Default", sizeof(d[0].name)); d[0].id = 0; return 1; }
    return 0;
}

static int hal_get_output_devices(AudioDeviceInfo *d, int max) {
    int c = 0;
    if (c < max) { strncpy(d[c].name, "Speaker", sizeof(d[c].name)); d[c].id = 2; c++; }
    if (c < max) { strncpy(d[c].name, "Auto", sizeof(d[c].name)); d[c].id = 0; c++; }
    return c;
}

static int hal_set_input_device(int id) { return 0; }
static int hal_set_output_device(int id) { return 0; }
static int hal_get_sample_rate(void) { return 48000; }
static int hal_get_buffer_size(void) { return 128; }
static int hal_get_xrun_count(void) { return 0; }

AudioHAL *audio_hal_create(void) {
    auto *hal = new AudioHAL();
    hal->init = hal_init; hal->start = hal_start; hal->stop = hal_stop;
    hal->destroy = hal_destroy;
    hal->get_input_devices = hal_get_input_devices;
    hal->get_output_devices = hal_get_output_devices;
    hal->set_input_device = hal_set_input_device;
    hal->set_output_device = hal_set_output_device;
    hal->get_sample_rate = hal_get_sample_rate;
    hal->get_buffer_size = hal_get_buffer_size;
    hal->get_xrun_count = hal_get_xrun_count;
    return hal;
}
