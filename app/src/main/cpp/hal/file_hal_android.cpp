#include "core/file_hal.h"
#include <android/log.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define LOG_TAG "FileHAL"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// These will be set from Java via JNI on app startup
static char g_audio_dir[512] = "";
static char g_presets_dir[512] = "";
static char g_cache_dir[512] = "";

// Called from JNI to set app-specific paths
extern "C" void file_hal_set_paths(const char *audio, const char *presets, const char *cache) {
    strncpy(g_audio_dir, audio, sizeof(g_audio_dir) - 1);
    strncpy(g_presets_dir, presets, sizeof(g_presets_dir) - 1);
    strncpy(g_cache_dir, cache, sizeof(g_cache_dir) - 1);
    LOGI("Paths set: audio=%s presets=%s cache=%s", audio, presets, cache);
}

static const char *hal_get_audio_dir(void) { return g_audio_dir; }
static const char *hal_get_presets_dir(void) { return g_presets_dir; }
static const char *hal_get_cache_dir(void) { return g_cache_dir; }

static FILE *hal_open_read(const char *path) { return fopen(path, "rb"); }
static FILE *hal_open_write(const char *path) { return fopen(path, "wb"); }

static int hal_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static int hal_mkdir_recursive(const char *path) {
    char tmp[512];
    strncpy(tmp, path, sizeof(tmp) - 1);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755) == 0 ? 0 : -1;
}

FileHAL *file_hal_create(void) {
    auto *hal = new FileHAL();
    hal->get_audio_dir = hal_get_audio_dir;
    hal->get_presets_dir = hal_get_presets_dir;
    hal->get_cache_dir = hal_get_cache_dir;
    hal->open_read = hal_open_read;
    hal->open_write = hal_open_write;
    hal->exists = hal_exists;
    hal->mkdir_recursive = hal_mkdir_recursive;
    return hal;
}
