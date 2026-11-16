#pragma once

#include <pebble.h>

void face_init(void);
void face_deinit(void);
void face_load(void);
bool face_set_frame(const uint8_t *data, int len);
void face_paint(Layer *layer, GContext *ctx);
