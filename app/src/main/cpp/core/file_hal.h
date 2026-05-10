#pragma once

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FileHAL FileHAL;

struct FileHAL {
    // Get app-specific directories
    const char *(*get_audio_dir)(void);
    const char *(*get_presets_dir)(void);
    const char *(*get_cache_dir)(void);

    // File operations
    FILE *(*open_read)(const char *path);
    FILE *(*open_write)(const char *path);
    int (*exists)(const char *path);
    int (*mkdir_recursive)(const char *path);
};

FileHAL *file_hal_create(void);

#ifdef __cplusplus
}
#endif
