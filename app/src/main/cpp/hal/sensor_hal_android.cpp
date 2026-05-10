#include "core/sensor_hal.h"
#include <android/log.h>
#include <android/sensor.h>
#include <android/looper.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define LOG_TAG "SensorHAL"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

struct SensorHALState {
    SensorCallback callback;
    void *user_data;
    ASensorManager *sensor_manager;
    ALooper *looper;
    ASensorRef accelerometer;
    ASensorRef gyroscope;
    ASensorRef proximity;
    ASensorEventQueue *event_queue;
    bool running;
    pthread_t looper_thread;
};

static SensorHALState *g_sensor_state = nullptr;

static int get_sensor_type(int type) {
    switch (type) {
        case SENSOR_GYRO_Y: return ASENSOR_TYPE_GYROSCOPE;
        case SENSOR_ACCEL_Z: return ASENSOR_TYPE_ACCELEROMETER;
        case SENSOR_PROXIMITY: return ASENSOR_TYPE_PROXIMITY;
        default: return -1;
    }
}

static int sensor_callback_func(int fd, int events, void *data) {
    auto *state = static_cast<SensorHALState *>(data);
    ASensorEvent event;
    while (ASensorEventQueue_getEvents(state->event_queue, &event, 1) > 0) {
        if (state->callback) {
            switch (event.type) {
                case ASENSOR_TYPE_GYROSCOPE:
                    state->callback(SENSOR_GYRO_Y, event.vector.y, state->user_data);
                    break;
                case ASENSOR_TYPE_ACCELEROMETER:
                    state->callback(SENSOR_ACCEL_Z, event.acceleration.z, state->user_data);
                    break;
                case ASENSOR_TYPE_PROXIMITY:
                    state->callback(SENSOR_PROXIMITY, event.distance, state->user_data);
                    break;
            }
        }
    }
    return 1;
}

static int sensor_init(SensorCallback cb, void *userdata) {
    g_sensor_state = new SensorHALState();
    g_sensor_state->callback = cb;
    g_sensor_state->user_data = userdata;
    g_sensor_state->running = false;

    g_sensor_state->sensor_manager = ASensorManager_getInstanceForPackage("com.guitarfx");
    g_sensor_state->accelerometer = ASensorManager_getDefaultSensor(
        g_sensor_state->sensor_manager, ASENSOR_TYPE_ACCELEROMETER);
    g_sensor_state->gyroscope = ASensorManager_getDefaultSensor(
        g_sensor_state->sensor_manager, ASENSOR_TYPE_GYROSCOPE);
    g_sensor_state->proximity = ASensorManager_getDefaultSensor(
        g_sensor_state->sensor_manager, ASENSOR_TYPE_PROXIMITY);

    g_sensor_state->looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    g_sensor_state->event_queue = ASensorManager_createEventQueue(
        g_sensor_state->sensor_manager, g_sensor_state->looper,
        ALOOPER_POLL_CALLBACK, sensor_callback_func, g_sensor_state);

    LOGI("Sensor HAL initialized");
    return 0;
}

static int sensor_start(int sensor_type) {
    if (!g_sensor_state) return -1;
    ASensorRef sensor = nullptr;
    switch (sensor_type) {
        case SENSOR_GYRO_Y: sensor = g_sensor_state->gyroscope; break;
        case SENSOR_ACCEL_Z: sensor = g_sensor_state->accelerometer; break;
        case SENSOR_PROXIMITY: sensor = g_sensor_state->proximity; break;
    }
    if (sensor) {
        ASensorEventQueue_enableSensor(g_sensor_state->event_queue, sensor);
        ASensorEventQueue_setEventRate(g_sensor_state->event_queue, sensor, 10000); // 10ms
        LOGI("Sensor %d enabled", sensor_type);
        return 0;
    }
    return -1;
}

static void sensor_stop(int sensor_type) {
    if (!g_sensor_state) return;
    ASensorRef sensor = nullptr;
    switch (sensor_type) {
        case SENSOR_GYRO_Y: sensor = g_sensor_state->gyroscope; break;
        case SENSOR_ACCEL_Z: sensor = g_sensor_state->accelerometer; break;
        case SENSOR_PROXIMITY: sensor = g_sensor_state->proximity; break;
    }
    if (sensor && g_sensor_state->event_queue) {
        ASensorEventQueue_disableSensor(g_sensor_state->event_queue, sensor);
    }
}

static void sensor_stop_all(void) {
    sensor_stop(SENSOR_GYRO_Y);
    sensor_stop(SENSOR_ACCEL_Z);
    sensor_stop(SENSOR_PROXIMITY);
}

static void sensor_destroy(void) {
    if (g_sensor_state) {
        sensor_stop_all();
        if (g_sensor_state->event_queue && g_sensor_state->sensor_manager) {
            ASensorManager_destroyEventQueue(g_sensor_state->sensor_manager,
                                             g_sensor_state->event_queue);
        }
        delete g_sensor_state;
        g_sensor_state = nullptr;
    }
}

SensorHAL *sensor_hal_create(void) {
    auto *hal = new SensorHAL();
    hal->init = sensor_init;
    hal->start = sensor_start;
    hal->stop = sensor_stop;
    hal->stop_all = sensor_stop_all;
    hal->destroy = sensor_destroy;
    return hal;
}
