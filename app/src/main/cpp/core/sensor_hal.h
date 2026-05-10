#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Sensor types
#define SENSOR_GYRO_Y      0
#define SENSOR_ACCEL_Z     1
#define SENSOR_PROXIMITY   2

typedef void (*SensorCallback)(int sensor_type, float value, void *userdata);

typedef struct SensorHAL SensorHAL;

struct SensorHAL {
    int (*init)(SensorCallback cb, void *userdata);
    int (*start)(int sensor_type);
    void (*stop)(int sensor_type);
    void (*stop_all)(void);
    void (*destroy)(void);
};

SensorHAL *sensor_hal_create(void);

#ifdef __cplusplus
}
#endif
