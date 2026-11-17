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
  .time = {60, 183, 140, 48},
  .temp = {0, 0, 0, 0},
  .date = {0, 0, 0, 0},
  .place = {0, 0, 0, 0},
  .meta = {40, 163, 180, 19},
  .cloud = {36, 56, 188, 15},
  .plot = {36, 72, 188, 59},
  .wind = {36, 132, 188, 13},
  .precip = {36, 146, 188, 15},
  .bt_x = 130,
  .bt_y = 22,
  .bt_r = 4,
};
#else
static const Layout LAYOUT = {
  .steps = {4, 1, 64, 16},
  .windnow = {68, 1, 64, 16},
  .battery = {132, 1, 64, 16},
  .time = {0, 165, 200, 57},
  .temp = {0, 0, 0, 0},
  .date = {0, 0, 0, 0},
  .place = {0, 0, 0, 0},
  .meta = {4, 146, 192, 19},
  .cloud = {4, 20, 192, 16},
  .plot = {4, 37, 192, 76},
  .wind = {4, 114, 192, 14},
  .precip = {4, 129, 192, 15},
  .bt_x = 8,
  .bt_y = 193,
  .bt_r = 4,
};
#endif
