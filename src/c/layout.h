#pragma once

#include <stdint.h>

typedef struct {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
} Rect;

typedef struct {
  Rect steps;
  Rect windnow;
  Rect battery;
  Rect time;
  Rect temp;
  Rect date;
  Rect place;
  Rect meta;
  Rect cloud;
  Rect plot;
  Rect wind;
  Rect precip;
  int16_t bt_x;
  int16_t bt_y;
  int16_t bt_r;
} Layout;

#if defined(PBL_ROUND)
static const Layout LAYOUT = {
  .steps = {54, 36, 52, 16},
  .windnow = {106, 36, 48, 16},
  .battery = {154, 36, 52, 16},
  .time = {60, 197, 140, 34},
  .temp = {0, 0, 0, 0},
  .date = {0, 0, 0, 0},
  .place = {0, 0, 0, 0},
  .meta = {40, 178, 180, 19},
  .cloud = {36, 56, 188, 15},
  .plot = {36, 72, 188, 74},
  .wind = {36, 147, 188, 13},
  .precip = {36, 161, 188, 15},
  .bt_x = 130,
  .bt_y = 22,
  .bt_r = 4,
};
#else
static const Layout LAYOUT = {
  .steps = {10, 1, 60, 16},
  .windnow = {70, 1, 60, 16},
  .battery = {130, 1, 60, 16},
  .time = {0, 151, 200, 52},
  .temp = {0, 0, 0, 0},
  .date = {0, 0, 0, 0},
  .place = {0, 0, 0, 0},
  .meta = {10, 206, 180, 19},
  .cloud = {10, 20, 180, 16},
  .plot = {10, 37, 180, 78},
  .wind = {10, 116, 180, 14},
  .precip = {10, 131, 180, 15},
  .bt_x = 14,
  .bt_y = 177,
  .bt_r = 4,
};
#endif
