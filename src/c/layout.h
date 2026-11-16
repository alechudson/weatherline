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
  Rect battery;
  Rect time;
  Rect temp;
  Rect date;
  Rect place;
  Rect meta;
  Rect cloud;
  Rect plot;
  Rect precip;
  int16_t bt_x;
  int16_t bt_y;
  int16_t bt_r;
} Layout;

#if defined(PBL_ROUND)
static const Layout LAYOUT = {
  .steps = {58, 212, 72, 16},
  .battery = {150, 212, 52, 16},
  .time = {55, 34, 150, 42},
  .temp = {70, 78, 120, 28},
  .date = {0, 0, 0, 0},
  .place = {0, 0, 0, 0},
  .meta = {40, 108, 180, 16},
  .cloud = {36, 126, 188, 14},
  .plot = {36, 140, 188, 52},
  .precip = {36, 192, 188, 18},
  .bt_x = 140,
  .bt_y = 220,
  .bt_r = 4,
};
#else
static const Layout LAYOUT = {
  .steps = {2, 0, 104, 18},
  .battery = {128, 0, 70, 18},
  .time = {2, 18, 124, 44},
  .temp = {120, 22, 78, 32},
  .date = {2, 62, 96, 18},
  .place = {100, 62, 98, 18},
  .meta = {0, 0, 0, 0},
  .cloud = {2, 82, 196, 16},
  .plot = {2, 98, 196, 100},
  .precip = {2, 198, 196, 28},
  .bt_x = 117,
  .bt_y = 9,
  .bt_r = 4,
};
#endif
