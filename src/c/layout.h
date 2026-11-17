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
  .steps = {54, 210, 52, 16},
  .windnow = {106, 210, 48, 16},
  .battery = {154, 210, 52, 16},
  .time = {60, 30, 140, 48},
  .temp = {0, 0, 0, 0},
  .date = {0, 0, 0, 0},
  .place = {0, 0, 0, 0},
  .meta = {40, 80, 180, 19},
  .cloud = {36, 101, 188, 15},
  .plot = {36, 117, 188, 59},
  .wind = {36, 177, 188, 13},
  .precip = {36, 191, 188, 15},
  .bt_x = 130,
  .bt_y = 16,
  .bt_r = 4,
};
#else
static const Layout LAYOUT = {
  .steps = {4, 210, 64, 16},
  .windnow = {68, 210, 64, 16},
  .battery = {132, 210, 64, 16},
  .time = {0, 2, 200, 57},
  .temp = {0, 0, 0, 0},
  .date = {0, 0, 0, 0},
  .place = {0, 0, 0, 0},
  .meta = {4, 61, 192, 19},
  .cloud = {4, 82, 192, 16},
  .plot = {4, 99, 192, 78},
  .wind = {4, 178, 192, 14},
  .precip = {4, 193, 192, 15},
  .bt_x = 193,
  .bt_y = 12,
  .bt_r = 4,
};
#endif
