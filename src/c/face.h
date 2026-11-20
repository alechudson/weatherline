#pragma once

#include <pebble.h>

void face_init(void);
void face_deinit(void);
void face_load(void);
bool face_set_frame(const uint8_t *data, int len);
// The center of the top line: wind, or the time of the last successful fetch.
void face_set_top_line(bool updated);
void face_paint(Layer *layer, GContext *ctx);
