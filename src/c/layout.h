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
  Rect wind;
  Rect precip;
  int16_t bt_x;
  int16_t bt_y;
  int16_t bt_r;
} Layout;

#if defined(PBL_ROUND)
static const Layout LAYOUT = {
  .steps = {58, 210, 72, 16},
  .battery = {150, 210, 52, 16},
  .time = {60, 28, 140, 50},
  .temp = {0, 0, 0, 0},
  .date = {0, 0, 0, 0},
  .place = {0, 0, 0, 0},
  .meta = {40, 76, 180, 24},
  .cloud = {36, 102, 188, 15},
  .plot = {36, 118, 188, 58},
  .wind = {36, 177, 188, 13},
  .precip = {36, 191, 188, 15},
  .bt_x = 140,
  .bt_y = 218,
  .bt_r = 4,
};
#else
static const Layout LAYOUT = {
  .steps = {4, 210, 92, 16},
  .battery = {104, 210, 92, 16},
  .time = {0, 3, 200, 50},
  .temp = {0, 0, 0, 0},
  .date = {0, 0, 0, 0},
  .place = {0, 0, 0, 0},
  .meta = {4, 53, 192, 24},
  .cloud = {4, 79, 192, 16},
  .plot = {4, 96, 192, 81},
  .wind = {4, 178, 192, 14},
  .precip = {4, 193, 192, 15},
  .bt_x = 100,
  .bt_y = 218,
  .bt_r = 4,
};
#endif
