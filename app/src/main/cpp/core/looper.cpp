#include "looper.h"

void looper_init(Looper *l, int sample_rate) {
    memset(l, 0, sizeof(Looper));
    l->sample_rate = sample_rate;
    l->state = LOOPER_IDLE;
    l->active_layer = -1;
}

void looper_free(Looper *l) {
    for (int i = 0; i < l->layer_count; i++) {
        if (l->layers[i].buffer) {
            free(l->layers[i].buffer);
            l->layers[i].buffer = nullptr;
        }
    }
    l->layer_count = 0;
}

void looper_record(Looper *l) {
    if (l->layer_count >= LOOPER_MAX_LAYERS) return;

    // Allocate new layer
    int idx = l->layer_count;
    l->layers[idx].buffer = (float *)calloc(LOOPER_MAX_FRAMES, sizeof(float));
    l->layers[idx].length = 0;
    l->layers[idx].volume = 1.0f;
    l->layers[idx].muted = 0;

    l->layer_count++;
    l->active_layer = idx;
    l->play_pos = 0;
    l->state = LOOPER_RECORDING;
}

void looper_stop(Looper *l) {
    if (l->state == LOOPER_RECORDING && l->active_layer >= 0) {
        // Finalize layer length
        l->layers[l->active_layer].length = l->play_pos;
        if (l->loop_length == 0) {
            l->loop_length = l->play_pos;
        }
        l->state = LOOPER_PLAYING;
        l->play_pos = 0;
    } else if (l->state == LOOPER_OVERDUBBING) {
        l->state = LOOPER_PLAYING;
    } else {
        l->state = LOOPER_IDLE;
    }
}

void looper_overdub(Looper *l) {
    if (l->layer_count >= LOOPER_MAX_LAYERS || l->loop_length == 0) return;

    // Add new layer
    int idx = l->layer_count;
    l->layers[idx].buffer = (float *)calloc(l->loop_length, sizeof(float));
    l->layers[idx].length = l->loop_length;
    l->layers[idx].volume = 1.0f;
    l->layers[idx].muted = 0;

    l->layer_count++;
    l->active_layer = idx;
    l->play_pos = 0;
    l->state = LOOPER_OVERDUBBING;
}

void looper_undo(Looper *l) {
    if (l->layer_count <= 0) return;

    int idx = l->layer_count - 1;
    if (l->layers[idx].buffer) {
        free(l->layers[idx].buffer);
        l->layers[idx].buffer = nullptr;
    }
    l->layer_count--;

    if (l->layer_count == 0) {
        l->state = LOOPER_IDLE;
        l->loop_length = 0;
        l->play_pos = 0;
    } else {
        l->state = LOOPER_PLAYING;
        l->play_pos = 0;
    }
}

float looper_process(Looper *l, float input) {
    float output = 0.0f;
    l->frames_processed++;

    switch (l->state) {
        case LOOPER_RECORDING:
            // Record input into active layer
            if (l->active_layer >= 0 && l->play_pos < LOOPER_MAX_FRAMES) {
                l->layers[l->active_layer].buffer[l->play_pos] = input;
            }
            // Also play back any previous layers
            for (int i = 0; i < l->active_layer; i++) {
                if (!l->layers[i].muted && l->play_pos < l->layers[i].length) {
                    output += l->layers[i].buffer[l->play_pos] * l->layers[i].volume;
                }
            }
            // Pass through input
            output += input;
            l->play_pos++;
            break;

        case LOOPER_PLAYING:
            // Mix all layers
            for (int i = 0; i < l->layer_count; i++) {
                if (!l->layers[i].muted && l->play_pos < l->layers[i].length) {
                    output += l->layers[i].buffer[l->play_pos] * l->layers[i].volume;
                }
            }
            l->play_pos++;
            if (l->loop_length > 0 && l->play_pos >= l->loop_length) {
                l->play_pos = 0;
            }
            break;

        case LOOPER_OVERDUBBING:
            // Record input into active layer while playing all
            if (l->active_layer >= 0 && l->play_pos < l->loop_length) {
                l->layers[l->active_layer].buffer[l->play_pos] = input;
            }
            for (int i = 0; i < l->layer_count; i++) {
                if (i == l->active_layer) continue;
                if (!l->layers[i].muted && l->play_pos < l->layers[i].length) {
                    output += l->layers[i].buffer[l->play_pos] * l->layers[i].volume;
                }
            }
            output += input;
            l->play_pos++;
            if (l->loop_length > 0 && l->play_pos >= l->loop_length) {
                l->play_pos = 0;
            }
            break;

        case LOOPER_IDLE:
        default:
            output = input;
            break;
    }

    float a = fabsf(output);
    if (a > l->peak) l->peak = a;
    return output;
}

void looper_set_layer_volume(Looper *l, int layer, float volume) {
    if (layer >= 0 && layer < l->layer_count) {
        l->layers[layer].volume = volume;
    }
}

void looper_set_layer_mute(Looper *l, int layer, int muted) {
    if (layer >= 0 && layer < l->layer_count) {
        l->layers[layer].muted = muted;
    }
}

int looper_get_layer_count(Looper *l) { return l->layer_count; }
LooperState looper_get_state(Looper *l) { return l->state; }
