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
  .steps = {58, 210, 72, 16},
  .battery = {150, 210, 52, 16},
  .time = {55, 32, 150, 40},
  .temp = {70, 72, 120, 30},
  .date = {0, 0, 0, 0},
  .place = {0, 0, 0, 0},
  .meta = {40, 102, 180, 18},
  .cloud = {36, 124, 188, 5},
  .plot = {36, 130, 188, 62},
  .precip = {36, 192, 188, 16},
  .bt_x = 140,
  .bt_y = 218,
  .bt_r = 4,
};
#else
static const Layout LAYOUT = {
  .steps = {4, 210, 92, 16},
  .battery = {104, 210, 92, 16},
  .time = {0, 2, 200, 46},
  .temp = {0, 46, 200, 30},
  .date = {0, 0, 0, 0},
  .place = {0, 0, 0, 0},
  .meta = {4, 76, 192, 18},
  .cloud = {4, 98, 192, 5},
  .plot = {4, 104, 192, 88},
  .precip = {4, 192, 192, 16},
  .bt_x = 100,
  .bt_y = 218,
  .bt_r = 4,
};
#endif
