#pragma once

#include <pebble.h>

void face_load(void);
bool face_set_frame(const uint8_t *data, int len);
void face_paint(Layer *layer, GContext *ctx);
