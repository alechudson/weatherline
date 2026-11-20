#pragma once

#include <pebble.h>

// Dirty region bitmask so normal minute ticks only repaint and flush the clock band.
#define FACE_DIRTY_TIME       (1 << 0)
#define FACE_DIRTY_CHART      (1 << 1)
#define FACE_DIRTY_STEPS      (1 << 2)
#define FACE_DIRTY_TOP_CENTER (1 << 3)
#define FACE_DIRTY_BATTERY    (1 << 4)
#define FACE_DIRTY_BT         (1 << 5)
#define FACE_DIRTY_META       (1 << 6)
#define FACE_DIRTY_ALL        0x7F

// Maximum binary frame size (v4: 40B header + 144*7B hours + 6*8B days).
#define FACE_MAX_BYTES 1096

void face_init(void);
void face_deinit(void);
void face_load(void);
bool face_has_frame(void);
bool face_needs_refresh(time_t now, uint32_t grace_sec);
// Unpacks and persists a new frame. Returns the dirty bitmask to repaint (0 if unchanged).
uint8_t face_set_frame(const uint8_t *data, int len);
// The center of the top line: wind, or the time of the last successful fetch.
bool face_set_top_line(bool updated);
// Computes which regions changed on a minute tick.
uint8_t face_tick_dirty(time_t now, TimeUnits units_changed);
bool face_battery_changed(BatteryChargeState charge);
bool face_bluetooth_changed(bool connected);
void face_invalidate_all(void);
void face_mark_dirty(uint8_t mask);
void face_paint(Layer *layer, GContext *ctx);
