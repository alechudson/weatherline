#include "face.h"
#include "layout.h"

#define FACE_VERSION 3
#define FACE_HOUR_BYTES 10
#define FACE_DAY_BYTES 8
#define FACE_MAX_HOURS 144
#define FACE_MAX_DAYS 6
#define FACE_MAX_SUNS 6
#define FACE_MAX_BYTES (40 + FACE_MAX_HOURS * FACE_HOUR_BYTES + FACE_MAX_DAYS * FACE_DAY_BYTES + FACE_MAX_SUNS * 4)
#define FACE_CHUNK 240
#define FACE_LEN_KEY 1
#define FACE_DATA_KEY 2
#define FACE_NO_ROW 255
#define FACE_NO_DIR 255
#define FACE_NO_SPEED 255
#define FACE_NO_DEG -32768

// The unit byte in the frame header carries this flag when the light theme is chosen.
#define FACE_LIGHT_FLAG 0x10
// And this one when the mono theme is chosen: black, white and grays only.
#define FACE_MONO_FLAG 0x20

#if defined(PBL_ROUND)
#define FACE_PLATFORM 2
#else
#define FACE_PLATFORM 1
#endif

typedef struct {
  uint8_t temp_y;
  uint8_t night;
  uint8_t rain_h;
  uint8_t snow_h;
  uint8_t cloud_h;
  uint8_t prev_y;
  uint8_t wind_h;
  uint8_t wind_dir;
  int8_t deg;
  uint8_t wind_speed;
} FaceHour;

typedef struct {
  uint8_t x;
  uint8_t span;
  int16_t max_deg;
  int16_t min_deg;
  uint8_t wday;
  uint8_t mday;
} FaceDay;

typedef struct {
  bool ok;
  bool light;
  bool mono;
  bool metric;
  uint8_t hour_count;
  uint8_t day_count;
  uint32_t fetched_at;
  uint32_t window_start;
  uint32_t window_end;
  int32_t utc_offset;
  int16_t current_deg;
  char place[16];
  FaceHour hours[FACE_MAX_HOURS];
  FaceDay days[FACE_MAX_DAYS];
} Face;

typedef struct {
  GColor bg;
  GColor text;
  GColor muted;
  GColor grid;
  GColor warmer;
  GColor colder;
  GColor rain;
  GColor snow;
  GColor now;
  GColor sky_cloud;
  GColor sky_night;
  GColor cloud_rim;
  GColor wind;
  GColor wind_edge;
  GColor wind_arrow;
  GColor wind_strong;
  // The signature accent: the now marker and AM/PM.
  GColor accent;
  // The temperature line's color by how warm it is: freezing, cool, mild, warm and hot.
  GColor thermal[5];
  // Mono has no hue to tell colder from warmer, so colder is drawn as stripes, and the line
  // grows thicker as it warms instead of changing color.
  bool striped_colder;
  bool thermal_width;
  // Mono marks night with a thin bar under the chart instead of a band behind it.
  bool night_bar;
} Theme;

static Face s_face;
static Face s_scratch;
static uint8_t s_blob[FACE_MAX_BYTES];

static const char *WEEKDAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// One palette per theme: grays for clouds and wind, a warm and a cool tone for warmer and
// colder than yesterday, blue only for water, a quiet band for night, a ramp for the line
// that tints only toward hot and freezing, and one apricot accent.
static Theme theme(void) {
  Theme t;
  t.striped_colder = false;
  t.thermal_width = false;
  t.night_bar = false;
  // Until the first forecast arrives, the face is dark.
  if (s_face.mono) {
    // Brightness carries the meaning: warmer is a bright checker, colder dim stripes, and
    // night a dark gray band.
    t.bg = GColorBlack;
    t.text = GColorWhite;
    t.muted = GColorLightGray;
    t.grid = GColorDarkGray;
    t.warmer = GColorWhite;
    t.colder = GColorLightGray;
    t.striped_colder = true;
    t.night_bar = true;
    t.rain = GColorLightGray;
    t.snow = GColorWhite;
    t.now = GColorWhite;
    t.sky_cloud = GColorLightGray;
    t.sky_night = GColorLightGray;
    t.cloud_rim = GColorWhite;
    t.wind = GColorDarkGray;
    t.wind_edge = GColorLightGray;
    t.wind_arrow = GColorLightGray;
    t.wind_strong = GColorWhite;
    t.accent = GColorWhite;
    t.thermal[0] = GColorWhite;
    t.thermal[1] = GColorWhite;
    t.thermal[2] = GColorWhite;
    t.thermal[3] = GColorWhite;
    t.thermal[4] = GColorWhite;
    t.thermal_width = true;
  } else if (s_face.light) {
    t.bg = GColorWhite;
    t.text = GColorBlack;
    t.muted = GColorDarkGray;
    t.grid = GColorLightGray;
    t.warmer = GColorOrange;
    t.colder = GColorLiberty;
    t.rain = GColorVividCerulean;
    t.snow = GColorDarkGray;
    t.now = GColorBlack;
    t.sky_cloud = GColorLightGray;
    t.sky_night = GColorCeleste;
    t.cloud_rim = GColorDarkGray;
    t.wind = GColorLightGray;
    t.wind_edge = GColorDarkGray;
    t.wind_arrow = GColorDarkGray;
    t.wind_strong = GColorWindsorTan;
    t.accent = GColorOrange;
    t.thermal[0] = GColorBlueMoon;
    t.thermal[1] = GColorCobaltBlue;
    t.thermal[2] = GColorBlack;
    t.thermal[3] = GColorWindsorTan;
    t.thermal[4] = GColorRed;
  } else {
    t.bg = GColorBlack;
    t.text = GColorWhite;
    t.muted = GColorLightGray;
    t.grid = GColorDarkGray;
    t.warmer = GColorRajah;
    t.colder = GColorBabyBlueEyes;
    t.rain = GColorPictonBlue;
    t.snow = GColorWhite;
    t.now = GColorWhite;
    t.sky_cloud = GColorLightGray;
    t.sky_night = GColorOxfordBlue;
    t.cloud_rim = GColorWhite;
    t.wind = GColorDarkGray;
    t.wind_edge = GColorLightGray;
    t.wind_arrow = GColorWhite;
    t.wind_strong = GColorIcterine;
    t.accent = GColorRajah;
    // Mild is plain white; the line only tints toward the extremes.
    t.thermal[0] = GColorPictonBlue;
    t.thermal[1] = GColorCeleste;
    t.thermal[2] = GColorWhite;
    t.thermal[3] = GColorPastelYellow;
    t.thermal[4] = GColorRajah;
  }
  return t;
}

static uint32_t read_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t read_i32(const uint8_t *p) {
  return (int32_t)read_u32(p);
}

static int16_t read_i16(const uint8_t *p) {
  return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int column_x(int i, int n, int plot_w) {
  return (i * plot_w) / n;
}

static int column_w(int i, int n, int plot_w) {
  return ((i + 1) * plot_w) / n - column_x(i, n, plot_w);
}

static int column_mid(int i, int n, int plot_w) {
  return column_x(i, n, plot_w) + column_w(i, n, plot_w) / 2;
}

static bool unpack(Face *face, const uint8_t *data, int len) {
  memset(face, 0, sizeof(*face));
  if (!data || len < 40 || len > FACE_MAX_BYTES) return false;
  if (data[0] != FACE_VERSION || data[1] != FACE_PLATFORM) return false;
  uint8_t hour_count = data[3];
  uint8_t day_count = data[22];
  uint8_t sun_count = data[23];
  int expect = 40 + (int)hour_count * FACE_HOUR_BYTES + (int)day_count * FACE_DAY_BYTES + (int)sun_count * 4;
  if (expect != len) return false;
  if (hour_count > FACE_MAX_HOURS || day_count > FACE_MAX_DAYS || sun_count > FACE_MAX_SUNS) {
    return false;
  }

  face->ok = true;
  face->light = (data[2] & FACE_LIGHT_FLAG) != 0;
  face->mono = !face->light && (data[2] & FACE_MONO_FLAG) != 0;
  face->metric = (data[2] & 0x0F) == 2;
  face->hour_count = hour_count;
  face->day_count = day_count;
  face->fetched_at = read_u32(data + 4);
  face->window_start = read_u32(data + 8);
  face->window_end = read_u32(data + 12);
  face->utc_offset = read_i32(data + 16);
  face->current_deg = read_i16(data + 20);
  memcpy(face->place, data + 24, 16);
  face->place[15] = 0;

  const uint8_t *p = data + 40;
  int i;
  for (i = 0; i < hour_count; i++) {
    face->hours[i].temp_y = p[0];
    face->hours[i].night = p[1];
    face->hours[i].rain_h = p[2];
    face->hours[i].snow_h = p[3];
    face->hours[i].cloud_h = p[4];
    face->hours[i].prev_y = p[5];
    face->hours[i].wind_h = p[6];
    face->hours[i].wind_dir = p[7];
    face->hours[i].deg = (int8_t)p[8];
    face->hours[i].wind_speed = p[9];
    p += FACE_HOUR_BYTES;
  }
  for (i = 0; i < day_count; i++) {
    face->days[i].x = p[0];
    face->days[i].span = p[1];
    face->days[i].max_deg = read_i16(p + 2);
    face->days[i].min_deg = read_i16(p + 4);
    face->days[i].wday = p[6];
    face->days[i].mday = p[7];
    p += FACE_DAY_BYTES;
  }
  // Sun records follow; this design draws the sky strip instead, so they are skipped.
  return true;
}

// Chunks land before the length, so a reboot mid-write keeps the previous frame.
static void save_bytes(const uint8_t *data, int len) {
  int off = 0;
  uint32_t key = FACE_DATA_KEY;
  while (off < len) {
    int n = len - off;
    if (n > FACE_CHUNK) n = FACE_CHUNK;
    int wrote = persist_write_data(key, data + off, (size_t)n);
    if (wrote < 0) return;
    key += 1;
    off += n;
  }
  persist_write_int(FACE_LEN_KEY, len);
}

bool face_set_frame(const uint8_t *data, int len) {
  if (!unpack(&s_scratch, data, len)) return false;
  s_face = s_scratch;
  save_bytes(data, len);
  return true;
}

void face_load(void) {
  if (!persist_exists(FACE_LEN_KEY)) return;
  int len = persist_read_int(FACE_LEN_KEY);
  if (len <= 0 || len > FACE_MAX_BYTES) return;
  int off = 0;
  uint32_t key = FACE_DATA_KEY;
  while (off < len) {
    int n = len - off;
    if (n > FACE_CHUNK) n = FACE_CHUNK;
    int got = persist_read_data(key, s_blob + off, (size_t)n);
    if (got != n) return;
    key += 1;
    off += n;
  }
  if (!unpack(&s_scratch, s_blob, len)) return;
  s_face = s_scratch;
}

static void fill_rect(GContext *ctx, int x, int y, int w, int h, GColor color) {
  if (w <= 0 || h <= 0) return;
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, GRect(x, y, w, h), 0, GCornerNone);
}

static int local_seconds(int hour_index) {
  return (int)(((int64_t)s_face.window_start + (int64_t)hour_index * 3600 + s_face.utc_offset) % 86400);
}

static GSize text_size(const char *text, GFont font) {
  return graphics_text_layout_get_content_size(
      text, font, GRect(0, 0, 160, 30), GTextOverflowModeWordWrap, GTextAlignmentLeft);
}

static void draw_text(GContext *ctx, const char *text, GFont font, GRect box,
                      GTextAlignment align, GColor color) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, box, GTextOverflowModeTrailingEllipsis, align, NULL);
}

// The color behind the chart at screen column x: the night backdrop or the background.
static GColor backdrop_at(const Theme *t, int x) {
  int n = s_face.hour_count;
  int px = x - LAYOUT.plot.x;
  if (n == 0 || px < 0 || px >= LAYOUT.plot.w) return t->bg;
  int i = (px * n) / LAYOUT.plot.w;
  while (i > 0 && column_x(i, n, LAYOUT.plot.w) > px) i -= 1;
  while (i < n - 1 && column_x(i + 1, n, LAYOUT.plot.w) <= px) i += 1;
  return s_face.hours[i].night && !t->night_bar ? t->sky_night : t->bg;
}

// Text that sits on the chart gets a one-pixel halo in whatever color is behind it.
static void draw_halo_text(GContext *ctx, const Theme *t, const char *text, GFont font,
                           int x, int y, GSize size, GColor color) {
  static const int8_t OFFSETS[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
  GColor halo = backdrop_at(t, x + size.w / 2);
  int k;
  for (k = 0; k < 4; k++) {
    draw_text(ctx, text, font, GRect(x + OFFSETS[k][0], y + OFFSETS[k][1], size.w, size.h),
              GTextAlignmentLeft, halo);
  }
  draw_text(ctx, text, font, GRect(x, y, size.w, size.h), GTextAlignmentLeft, color);
}

// Vertically centers one line of text in a box.
static void draw_fitted(GContext *ctx, const char *text, GFont font, GRect box,
                        GTextAlignment align, GColor color) {
  if (!text || !text[0] || box.size.w <= 0 || box.size.h <= 0) return;
  GSize size = graphics_text_layout_get_content_size(
      text, font, GRect(0, 0, 400, box.size.h), GTextOverflowModeWordWrap, GTextAlignmentLeft);
  if (size.h > 0 && size.h < box.size.h) {
    box.origin.y += (box.size.h - size.h) / 2;
    box.size.h = size.h;
  }
  draw_text(ctx, text, font, box, align, color);
}

// ---- Chart ----

// Night hours get a dark backdrop that runs from the sky lane down through the rain lane.
#define NIGHT_FADE 3

// A few columns of ordered dither, a quarter, then half, then three quarters filled, walking
// from `x` in direction `dir` toward the solid band.
static void draw_night_fade(GContext *ctx, const Theme *t, int x, int top, int bottom, int cols,
                            int dir) {
  int k;
  graphics_context_set_stroke_color(ctx, t->sky_night);
  for (k = 0; k < cols; k++) {
    int cx = x + dir * k;
    int y;
    for (y = top; y < bottom; y++) {
      bool on;
      if (k == 0) {
        on = ((cx | y) & 1) == 0;
      } else if (k == 1) {
        on = ((cx + y) & 1) == 0;
      } else {
        on = ((cx & y) & 1) == 0;
      }
      if (on) graphics_draw_pixel(ctx, GPoint(cx, y));
    }
  }
}

static void draw_night(GContext *ctx, const Theme *t) {
  int n = s_face.hour_count;
  int w = LAYOUT.plot.w;
  int top = LAYOUT.cloud.y;
  int bottom = LAYOUT.precip.y + LAYOUT.precip.h;
  int i = 0;
  while (i < n) {
    if (!s_face.hours[i].night) {
      i += 1;
      continue;
    }
    int j = i;
    while (j < n && s_face.hours[j].night) j += 1;
    int x0 = column_x(i, n, w);
    int x1 = column_x(j, n, w);
    if (t->night_bar) {
      fill_rect(ctx, LAYOUT.plot.x + x0, bottom, x1 - x0, 2, t->sky_night);
    } else {
      // Dusk and dawn fade over a few columns instead of starting with a hard edge. The
      // chart's own sides stay square.
      int fade_l = x0 > 0 ? NIGHT_FADE : 0;
      int fade_r = x1 < w ? NIGHT_FADE : 0;
      if (fade_l + fade_r > x1 - x0) fade_l = fade_r = 0;
      fill_rect(ctx, LAYOUT.plot.x + x0 + fade_l, top, x1 - x0 - fade_l - fade_r, bottom - top,
                t->sky_night);
      draw_night_fade(ctx, t, LAYOUT.plot.x + x0, top, bottom, fade_l, 1);
      draw_night_fade(ctx, t, LAYOUT.plot.x + x1 - 1, top, bottom, fade_r, -1);
    }
    i = j;
  }
}

// Value of an hourly field at pixel column px, interpolated between hour centers.
static int field_at(int px, const uint8_t *base, int stride) {
  int n = s_face.hour_count;
  int w = LAYOUT.plot.w;
  int i = 0;
  while (i < n - 1 && px >= column_mid(i + 1, n, w)) i += 1;
  int a = base[i * stride];
  int mid = column_mid(i, n, w);
  if (i >= n - 1 || px <= mid) return a;
  int b = base[(i + 1) * stride];
  int next = column_mid(i + 1, n, w);
  if (next <= mid) return a;
  return a + ((b - a) * (px - mid)) / (next - mid);
}

static int cloud_at(int px) {
  return field_at(px, &s_face.hours[0].cloud_h, sizeof(FaceHour));
}

// Clouds are a bank of puffs sitting on the lane floor. Each puff grows with the cover
// under it, so thin cloud is a low ripple and overcast fills the lane. A one-pixel rim on
// top of each puff gives the bank some shape.
static void draw_clouds(GContext *ctx, const Theme *t) {
  int w = LAYOUT.plot.w;
  int lane = LAYOUT.cloud.h;
  int floor_y = LAYOUT.cloud.y + lane;
  int max_r = lane / 2;
  int step = 5;
  // Puff sizes cycle through this pattern so the top edge billows instead of running flat.
  static const int8_t BILLOW[] = {0, -2, -1, -3, 0, -1, -2};
  int pass;
  graphics_context_set_antialiased(ctx, true);
  for (pass = 0; pass < 2; pass++) {
    int px;
    int k = 0;
    graphics_context_set_fill_color(ctx, pass == 0 ? t->cloud_rim : t->sky_cloud);
    for (px = 0; px < w; px += step, k++) {
      int c = cloud_at(px);
      int r = (c * max_r + lane / 2) / lane;
      if (r >= 3) r += BILLOW[k % (int)ARRAY_LENGTH(BILLOW)];
      if (r < 2) continue;
      // Keep each puff inside the chart's width.
      int x = LAYOUT.plot.x + px;
      if (x < LAYOUT.plot.x + r) x = LAYOUT.plot.x + r;
      if (x > LAYOUT.plot.x + w - 1 - r) x = LAYOUT.plot.x + w - 1 - r;
      int y = floor_y - r;
      int rr = pass == 0 ? r : r - 1;
      if (rr < 1) continue;
      graphics_fill_circle(ctx, GPoint(x, y), (uint16_t)rr);
      // Square off the bottom half so the bank sits flat on the floor.
      int left = x - rr;
      int right = x + rr;
      if (left < LAYOUT.plot.x) left = LAYOUT.plot.x;
      if (right > LAYOUT.plot.x + w) right = LAYOUT.plot.x + w;
      if (pass == 1 && right > left) {
        graphics_fill_rect(ctx, GRect(left, y, right - left + 1, floor_y - y), 0, GCornerNone);
      }
    }
  }
}

// The sky lane holds only clouds; an empty lane means a clear sky.
static void draw_sky(GContext *ctx, const Theme *t) {
  draw_clouds(ctx, t);
}

// Row of the line at pixel column px, interpolated between hour centers.
static int row_at(int px, bool prev, bool *known) {
  int n = s_face.hour_count;
  int w = LAYOUT.plot.w;
  int i = 0;
  while (i < n - 1 && px >= column_mid(i + 1, n, w)) i += 1;
  int a = prev ? s_face.hours[i].prev_y : s_face.hours[i].temp_y;
  if (prev && a == FACE_NO_ROW) {
    *known = false;
    return 0;
  }
  *known = true;
  int mid = column_mid(i, n, w);
  if (i >= n - 1 || px <= mid) return a;
  int b = prev ? s_face.hours[i + 1].prev_y : s_face.hours[i + 1].temp_y;
  if (prev && b == FACE_NO_ROW) return a;
  int next = column_mid(i + 1, n, w);
  if (next <= mid) return a;
  return a + ((b - a) * (px - mid)) / (next - mid);
}

// Shade between today and yesterday at the same hour: warm where today is warmer, cool where colder.
static void draw_fronts(GContext *ctx, const Theme *t) {
  int px;
  graphics_context_set_antialiased(ctx, false);
  for (px = 0; px < LAYOUT.plot.w; px++) {
    bool known_now;
    bool known_prev;
    int now_y = row_at(px, false, &known_now);
    int prev_y = row_at(px, true, &known_prev);
    if (!known_prev) continue;
    int x = LAYOUT.plot.x + px;
    int y0;
    int y1;
    bool striped = false;
    if (prev_y > now_y + 1) {
      graphics_context_set_stroke_color(ctx, t->warmer);
      y0 = now_y;
      y1 = prev_y;
    } else if (now_y > prev_y + 1) {
      graphics_context_set_stroke_color(ctx, t->colder);
      y0 = prev_y;
      y1 = now_y;
      striped = t->striped_colder;
    } else {
      continue;
    }
    // A checkerboard reads as a softer tone than a solid fill, so the line stays on top.
    int y;
    if (striped) {
      // Every other row, so colder reads as horizontal lines rather than a checker.
      for (y = y0 + 1; y < y1; y++) {
        if (((LAYOUT.plot.y + y) & 1) == 0) graphics_draw_pixel(ctx, GPoint(x, LAYOUT.plot.y + y));
      }
      continue;
    }
    for (y = y0 + ((y0 + px) & 1); y < y1; y += 2) {
      graphics_draw_pixel(ctx, GPoint(x, LAYOUT.plot.y + y));
    }
  }
}

// Which of the five thermal bands a temperature falls in, judged in Fahrenheit: freezing
// below 32, cool below 50, mild below 70, warm below 85, and hot from there.
static int thermal_band(int deg) {
  int f = s_face.metric ? (deg * 9) / 5 + 32 : deg;
  if (f < 32) return 0;
  if (f < 50) return 1;
  if (f < 70) return 2;
  if (f < 85) return 3;
  return 4;
}

// The line takes its color from how warm each stretch is (or, in mono, its width).
static void set_thermal_stroke(GContext *ctx, const Theme *t, int deg) {
  int band = thermal_band(deg);
  graphics_context_set_stroke_color(ctx, t->thermal[band]);
  if (t->thermal_width) {
    graphics_context_set_stroke_width(ctx, band == 0 ? 1 : (band >= 3 ? 3 : 2));
  } else {
    graphics_context_set_stroke_width(ctx, 2);
  }
}

static void draw_line(GContext *ctx, const Theme *t) {
  int n = s_face.hour_count;
  int w = LAYOUT.plot.w;
  int i;
  if (n < 2) return;
  graphics_context_set_antialiased(ctx, true);
  for (i = 0; i < n - 1; i++) {
    set_thermal_stroke(ctx, t, (s_face.hours[i].deg + s_face.hours[i + 1].deg) / 2);
    graphics_draw_line(ctx,
                       GPoint(LAYOUT.plot.x + column_mid(i, n, w), LAYOUT.plot.y + s_face.hours[i].temp_y),
                       GPoint(LAYOUT.plot.x + column_mid(i + 1, n, w), LAYOUT.plot.y + s_face.hours[i + 1].temp_y));
  }
  graphics_context_set_stroke_width(ctx, 1);
}

// The hour of the forecast that contains now, or -1 when now is outside the window.
static int hour_now(time_t now) {
  int64_t now64 = (int64_t)now;
  int64_t start = (int64_t)s_face.window_start;
  if (!s_face.ok || s_face.hour_count == 0 || now64 < start) return -1;
  int64_t i = (now64 - start) / 3600;
  return i < s_face.hour_count ? (int)i : -1;
}

static bool reading_fresh(time_t now) {
  int64_t now64 = (int64_t)now;
  int64_t fetched = (int64_t)s_face.fetched_at;
  return now64 >= fetched - 600 && now64 - fetched <= 3600;
}

// The temperature to show now: the phone's reading while it is under an hour old, then the
// forecast for the current hour, so the face stays right when the phone stops refreshing.
// Only when now has run past the whole forecast is the old reading shown, grayed.
static int temp_now(time_t now, bool *stale) {
  *stale = false;
  if (!s_face.ok) return FACE_NO_DEG;
  if (s_face.current_deg != FACE_NO_DEG && reading_fresh(now)) return s_face.current_deg;
  int i = hour_now(now);
  if (i >= 0) return s_face.hours[i].deg;
  *stale = true;
  return s_face.current_deg;
}

// The x of now on the chart, or -1 when now is outside the forecast window.
static int now_px(time_t now) {
  if (!s_face.ok || s_face.window_end <= s_face.window_start) return -1;
  int64_t now64 = (int64_t)now;
  if (now64 < (int64_t)s_face.window_start || now64 >= (int64_t)s_face.window_end) return -1;
  int64_t span = (int64_t)s_face.window_end - (int64_t)s_face.window_start;
  return (int)(((now64 - (int64_t)s_face.window_start) * LAYOUT.plot.w) / span);
}

// Where one day's labels go: its name and high along the top of the chart, its low along the
// bottom. The temperature tag reads the same boxes so it never lands on a label.
typedef struct {
  char name[6];
  char max[8];
  char min[8];
  GSize name_size;
  GSize max_size;
  GSize min_size;
  int x;
  int max_x;
  bool show_max;
  bool show_min;
} DayLabel;

static void layout_day_label(int i, DayLabel *out) {
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  GFont bold = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  FaceDay day = s_face.days[i];
  int span = day.span - 3;
  int gap = 2;
  memset(out, 0, sizeof(*out));
  snprintf(out->max, sizeof(out->max), "%d", day.max_deg);
  snprintf(out->min, sizeof(out->min), "%d", day.min_deg);
  out->max_size = text_size(out->max, bold);
  out->min_size = text_size(out->min, bold);
  // With several days on screen, name each one in a quieter color; drop the name if the
  // column is too narrow for it.
  if (s_face.day_count > 1 && day.wday <= 6) {
    snprintf(out->name, sizeof(out->name), "%s", WEEKDAYS[day.wday]);
    out->name_size = text_size(out->name, font);
    if (out->name_size.w + gap + out->max_size.w > span) out->name_size = GSizeZero;
  }
  out->x = LAYOUT.plot.x + day.x + 3;
  out->max_x = out->name_size.w > 0 ? out->x + out->name_size.w + gap : out->x;
  out->show_max = out->max_size.w > 0 && out->max_x + out->max_size.w <= out->x + span;
  out->show_min = out->min_size.w > 0 && out->min_size.w <= span;
}

#define LABEL_TOP_Y (LAYOUT.plot.y - 3)

static int label_bottom_y(const DayLabel *label) {
  return LAYOUT.plot.y + LAYOUT.plot.h - label->min_size.h;
}

// Whether a rectangle touches any day label's ink, which sits about 4 to 14 pixels into
// each 14-point text box.
static bool hits_day_label(GRect r) {
  int i;
  for (i = 0; i < s_face.day_count; i++) {
    DayLabel label;
    layout_day_label(i, &label);
    int top_x1 = label.show_max ? label.max_x + label.max_size.w : label.x + label.name_size.w;
    if (top_x1 > label.x && r.origin.x < top_x1 + 1 && r.origin.x + r.size.w > label.x - 1 &&
        r.origin.y < LABEL_TOP_Y + 15 && r.origin.y + r.size.h > LABEL_TOP_Y + 3) {
      return true;
    }
    int by = label_bottom_y(&label);
    if (label.show_min && r.origin.x < label.x + label.min_size.w + 1 &&
        r.origin.x + r.size.w > label.x - 1 && r.origin.y < by + 15 && r.origin.y + r.size.h > by + 3) {
      return true;
    }
  }
  return false;
}

static void draw_day_labels(GContext *ctx, const Theme *t) {
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  // Highs and lows are bold so they hold up against the night bands and shading.
  GFont bold = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  int i;
  for (i = 0; i < s_face.day_count; i++) {
    DayLabel label;
    layout_day_label(i, &label);
    if (label.name_size.w > 0) {
      draw_halo_text(ctx, t, label.name, font, label.x, LABEL_TOP_Y, label.name_size, t->muted);
    }
    if (label.show_max) {
      draw_halo_text(ctx, t, label.max, bold, label.max_x, LABEL_TOP_Y, label.max_size, t->text);
    }
    if (label.show_min) {
      draw_halo_text(ctx, t, label.min, bold, label.x, label_bottom_y(&label), label.min_size,
                     t->text);
    }
  }
}

// The current temperature rides the line in a small tag at now, filled with the line's color
// there. It sits above the marker, or below it when the line runs near the top.
static void draw_now_tag(GContext *ctx, const Theme *t, int x, int y, time_t now) {
  bool stale;
  int deg = temp_now(now, &stale);
  if (deg == FACE_NO_DEG) return;
  char text[12];
  snprintf(text, sizeof(text), "%d\xc2\xb0", deg);
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GSize size = text_size(text, font);
  int w = size.w + 8;
  int h = 15;
  // Try above the marker, then below, then to the right and left of it, and take the first
  // spot that stays in the chart and clear of the day labels.
  int cand_x[4] = {x - w / 2, x - w / 2, x + 9, x - 9 - w};
  int cand_y[4] = {y - 8 - h, y + 8, y - h / 2, y - h / 2};
  int tx = cand_x[0];
  int ty = cand_y[0];
  int k;
  for (k = 0; k < 4; k++) {
    int cx = cand_x[k];
    int cy = cand_y[k];
    if (cx < LAYOUT.plot.x) cx = LAYOUT.plot.x;
    if (cx + w > LAYOUT.plot.x + LAYOUT.plot.w) cx = LAYOUT.plot.x + LAYOUT.plot.w - w;
    if (cy < LAYOUT.plot.y - 2 || cy + h > LAYOUT.plot.y + LAYOUT.plot.h) continue;
    if (hits_day_label(GRect(cx, cy, w, h))) continue;
    tx = cx;
    ty = cy;
    break;
  }
  if (k == 4) {
    if (tx < LAYOUT.plot.x) tx = LAYOUT.plot.x;
    if (tx + w > LAYOUT.plot.x + LAYOUT.plot.w) tx = LAYOUT.plot.x + LAYOUT.plot.w - w;
    if (ty < LAYOUT.plot.y - 2) ty = y + 8;
  }
  GColor fill = stale ? t->muted : t->thermal[thermal_band(deg)];
  graphics_context_set_antialiased(ctx, true);
  // A ring in the background color keeps the tag apart from the line and shading behind it.
  graphics_context_set_fill_color(ctx, backdrop_at(t, tx + w / 2));
  graphics_fill_rect(ctx, GRect(tx - 1, ty - 1, w + 2, h + 2), 5, GCornersAll);
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_rect(ctx, GRect(tx, ty, w, h), 4, GCornersAll);
  draw_text(ctx, text, font, GRect(tx + 4, ty - 4, size.w + 2, 20), GTextAlignmentLeft, t->bg);
}

static void draw_now(GContext *ctx, const Theme *t, time_t now) {
  int px = now_px(now);
  if (px < 0) return;
  int x = LAYOUT.plot.x + px;
  int y0 = LAYOUT.cloud.y + LAYOUT.cloud.h;
  int yy;
  for (yy = y0; yy < LAYOUT.precip.y + LAYOUT.precip.h; yy += 2) fill_rect(ctx, x, yy, 1, 1, t->now);
  bool known;
  int y = LAYOUT.plot.y + row_at(px, false, &known);
  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_fill_color(ctx, t->accent);
  graphics_fill_circle(ctx, GPoint(x, y), 5);
  graphics_context_set_fill_color(ctx, t->bg);
  graphics_fill_circle(ctx, GPoint(x, y), 2);
  draw_now_tag(ctx, t, x, y, now);
}

static void draw_hour_labels(GContext *ctx, const Theme *t) {
  if (s_face.hour_count == 0 || s_face.hour_count > 24) return;
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  bool h24 = clock_is_24h_style();
  int n = s_face.hour_count;
  int i;
  for (i = 0; i < n; i++) {
    int hour = local_seconds(i) / 3600;
    if (local_seconds(i) < 0 || (hour != 6 && hour != 12 && hour != 18)) continue;
    char text[6];
    if (h24) {
      snprintf(text, sizeof(text), "%d", hour);
    } else {
      snprintf(text, sizeof(text), "%d%s", hour == 12 ? 12 : hour % 12, hour < 12 ? "a" : "p");
    }
    GSize size = text_size(text, font);
    int x = LAYOUT.plot.x + column_mid(i, n, LAYOUT.plot.w) - size.w / 2;
    draw_halo_text(ctx, t, text, font, x, LAYOUT.plot.y + LAYOUT.plot.h - size.h, size, t->muted);
  }
}

// Arrows go every few hours: the smallest step that divides a day and leaves room between them.
static int wind_step_hours(void) {
  static const int STEPS[] = {1, 2, 3, 4, 6, 8, 12, 24};
  int n = s_face.hour_count;
  int k;
  for (k = 0; k < (int)ARRAY_LENGTH(STEPS); k++) {
    if (STEPS[k] * LAYOUT.plot.w >= 13 * n) return STEPS[k];
  }
  return 24;
}

// An arrow pointing the way the wind blows, i.e. away from where it comes from.
static void draw_wind_arrow(GContext *ctx, int x, int y, int from_deg, int len) {
  int32_t angle = TRIG_MAX_ANGLE * ((from_deg + 180) % 360) / 360;
  int32_t s = sin_lookup(angle);
  int32_t c = cos_lookup(angle);
  int half = len / 2;
  GPoint tip = GPoint(x + (int)(s * half / TRIG_MAX_RATIO), y - (int)(c * half / TRIG_MAX_RATIO));
  GPoint tail = GPoint(x - (int)(s * half / TRIG_MAX_RATIO), y + (int)(c * half / TRIG_MAX_RATIO));
  graphics_draw_line(ctx, tail, tip);
  int head = 3 + len / 6;
  int k;
  for (k = -1; k <= 1; k += 2) {
    int32_t back = angle + TRIG_MAX_ANGLE / 2 + k * TRIG_MAX_ANGLE / 10;
    graphics_draw_line(ctx, tip,
                       GPoint(tip.x + (int)(sin_lookup(back) * head / TRIG_MAX_RATIO),
                              tip.y - (int)(cos_lookup(back) * head / TRIG_MAX_RATIO)));
  }
}

// The wind lane: speed is a soft area rising from the floor, and arrows every few hours show
// which way it blows.
static void draw_wind(GContext *ctx, const Theme *t) {
  int n = s_face.hour_count;
  int w = LAYOUT.plot.w;
  int top = LAYOUT.wind.y;
  int floor_y = LAYOUT.wind.y + LAYOUT.wind.h;
  int px;
  graphics_context_set_antialiased(ctx, false);
  graphics_context_set_stroke_width(ctx, 1);
  // The area gets a brighter top edge so its shape reads on the night bands too.
  for (px = 0; px < w; px++) {
    int h = field_at(px, &s_face.hours[0].wind_h, sizeof(FaceHour));
    if (h <= 0) continue;
    if (h > LAYOUT.wind.h) h = LAYOUT.wind.h;
    int x = LAYOUT.plot.x + px;
    graphics_context_set_stroke_color(ctx, t->wind);
    graphics_draw_line(ctx, GPoint(x, floor_y - h), GPoint(x, floor_y - 1));
    graphics_context_set_stroke_color(ctx, t->wind_edge);
    graphics_draw_pixel(ctx, GPoint(x, floor_y - h));
  }

  int step = wind_step_hours();
  int len = LAYOUT.wind.h - 2;
  int cy = top + LAYOUT.wind.h / 2;
  int i;
  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_stroke_width(ctx, 1);
  for (i = 0; i < n; i++) {
    int secs = local_seconds(i);
    if (secs < 0 || (secs / 3600) % step != step / 2) continue;
    FaceHour hour = s_face.hours[i];
    int x = LAYOUT.plot.x + column_mid(i, n, w);
    if (hour.wind_dir == FACE_NO_DIR) continue;
    if (hour.wind_h == 0) {
      // Calm: a dot instead of an arrow.
      graphics_context_set_fill_color(ctx, t->wind_arrow);
      graphics_fill_circle(ctx, GPoint(x, cy), 1);
      continue;
    }
    // Strong wind, from about 30 km/h (19 mph), turns its arrows warm.
    bool strong = hour.wind_h * 5 >= LAYOUT.wind.h * 3;
    graphics_context_set_stroke_color(ctx, strong ? t->wind_strong : t->wind_arrow);
    draw_wind_arrow(ctx, x, cy, hour.wind_dir * 2, len);
  }
}

// Rain is a bar per hour rising from the baseline; snow is a stack of flakes above any rain.
static void draw_precip(GContext *ctx, const Theme *t) {
  int n = s_face.hour_count;
  int w = LAYOUT.plot.w;
  int base = LAYOUT.precip.y + LAYOUT.precip.h;
  int i;
  fill_rect(ctx, LAYOUT.precip.x, base - 1, LAYOUT.precip.w, 1, t->grid);
  for (i = 0; i < n; i++) {
    int x = LAYOUT.plot.x + column_x(i, n, w);
    int cw = column_w(i, n, w);
    if (cw >= 4) cw -= 1;
    // A full bar stops two rows short of the lane top, leaving a gap under the wind lane.
    int rain = s_face.hours[i].rain_h;
    if (rain > LAYOUT.precip.h - 2) rain = LAYOUT.precip.h - 2;
    int snow = s_face.hours[i].snow_h;
    if (rain > 0) fill_rect(ctx, x, base - rain, cw, rain, t->rain);
    if (snow > 0) {
      int top = base - rain;
      int y;
      for (y = top - 2; y > top - snow - 1 && y >= LAYOUT.precip.y + 2; y -= 3) {
        fill_rect(ctx, x + cw / 2 - 1, y, 2, 2, t->snow);
      }
    }
  }
}

static void draw_chart(GContext *ctx, const Theme *t, time_t now) {
  draw_night(ctx, t);
  draw_sky(ctx, t);
  draw_fronts(ctx, t);
  draw_line(ctx, t);
  draw_wind(ctx, t);
  draw_precip(ctx, t);
  draw_day_labels(ctx, t);
  draw_hour_labels(ctx, t);
  draw_now(ctx, t, now);
}

static void draw_waiting(GContext *ctx, const Theme *t) {
  draw_text(ctx, "Waiting for the forecast from your phone",
            fonts_get_system_font(FONT_KEY_GOTHIC_18),
            GRect(LAYOUT.plot.x + 12, LAYOUT.plot.y + LAYOUT.plot.h / 2 - 22, LAYOUT.plot.w - 24, 44),
            GTextAlignmentCenter, t->muted);
}

// ---- Header ----

static GFont s_time_font;

// Rows of empty space above the digits in each time font's text box.
#if defined(PBL_ROUND)
#define TIME_TOP_PAD 19
#else
#define TIME_TOP_PAD 22
#endif

void face_init(void) {
#if defined(PBL_ROUND)
  s_time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TIME_67));
#else
  s_time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TIME_80));
#endif
}

void face_deinit(void) {
  if (s_time_font) fonts_unload_custom_font(s_time_font);
  s_time_font = NULL;
}

static void draw_time(GContext *ctx, const Theme *t, struct tm *local) {
  char text[8];
  const char *ampm = NULL;
  if (clock_is_24h_style()) {
    snprintf(text, sizeof(text), "%02d:%02d", local->tm_hour, local->tm_min);
  } else {
    int hour = local->tm_hour % 12;
    if (hour == 0) hour = 12;
    snprintf(text, sizeof(text), "%d:%02d", hour, local->tm_min);
    ampm = local->tm_hour < 12 ? "AM" : "PM";
  }
  GFont time_font = s_time_font ? s_time_font : fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
  GFont small = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  GSize time_size = graphics_text_layout_get_content_size(
      text, time_font, GRect(0, 0, 300, 100), GTextOverflowModeWordWrap, GTextAlignmentLeft);
  GSize am_size = ampm ? text_size(ampm, small) : GSizeZero;
  int gap = ampm ? 3 : 0;
  // Center the digits; AM or PM hangs off to the right without pushing them over, when it fits.
  int x = LAYOUT.time.x + (LAYOUT.time.w - time_size.w) / 2;
  if (ampm && x + time_size.w + gap + am_size.w > LAYOUT.time.x + LAYOUT.time.w) {
    x = LAYOUT.time.x + (LAYOUT.time.w - (time_size.w + gap + am_size.w)) / 2;
  }
  // The font's box has empty space above the digits; the layout's time box is the digits.
  int y = LAYOUT.time.y - TIME_TOP_PAD;
  draw_text(ctx, text, time_font, GRect(x, y, time_size.w + 4, time_size.h + 4), GTextAlignmentLeft,
            t->text);
  if (ampm) {
    draw_text(ctx, ampm, small,
              GRect(x + time_size.w + gap, LAYOUT.time.y + LAYOUT.time.h - am_size.h - 6,
                    am_size.w, am_size.h),
              GTextAlignmentLeft, t->accent);
  }
}

// One line under the time: the date and place, dropping the place when it does not fit. The
// temperature lives on the chart; it joins this line, grayed, only when now has run past the
// forecast and the chart has no marker to carry it.
static void draw_meta(GContext *ctx, const Theme *t, struct tm *local, time_t now) {
  const char *wday = (local->tm_wday >= 0 && local->tm_wday <= 6) ? WEEKDAYS[local->tm_wday] : "";
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  char date[16];
  char temp[16] = "";
  snprintf(date, sizeof(date), "%s %d", wday, local->tm_mday);
  if (s_face.ok && now_px(now) < 0) {
    bool stale;
    int deg = temp_now(now, &stale);
    if (deg != FACE_NO_DEG) snprintf(temp, sizeof(temp), "%d\xc2\xb0  \xc2\xb7  ", deg);
  }
  char line[64];
  snprintf(line, sizeof(line), "%s%s  \xc2\xb7  %s", temp, date, s_face.place);
  if (!s_face.ok || !s_face.place[0] || text_size(line, font).w > LAYOUT.meta.w) {
    snprintf(line, sizeof(line), "%s%s", temp, date);
  }
  draw_fitted(ctx, line, font, GRect(LAYOUT.meta.x, LAYOUT.meta.y, LAYOUT.meta.w, LAYOUT.meta.h),
              GTextAlignmentCenter, t->muted);
}

// ---- Status row ----

static void format_grouped(char *out, int value) {
  char raw[12];
  int n = snprintf(raw, sizeof(raw), "%d", value);
  if (n <= 0) {
    out[0] = 0;
    return;
  }
  int commas = (n - 1) / 3;
  int m = n + commas;
  out[m] = 0;
  int group = 0;
  int j = m - 1;
  int i;
  for (i = n - 1; i >= 0; i--) {
    out[j--] = raw[i];
    group += 1;
    if (group == 3 && i > 0) {
      out[j--] = ',';
      group = 0;
    }
  }
}

// The current hour's wind: an arrow the way it blows and the speed, with the unit when it fits.
static void draw_wind_now(GContext *ctx, const Theme *t, time_t now) {
  int i = hour_now(now);
  if (i < 0) return;
  FaceHour hour = s_face.hours[i];
  if (hour.wind_speed == FACE_NO_SPEED) return;
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  char text[16];
  snprintf(text, sizeof(text), "%d %s", hour.wind_speed, s_face.metric ? "km/h" : "mph");
  bool arrow = hour.wind_dir != FACE_NO_DIR && hour.wind_speed > 0;
  int arrow_w = arrow ? 11 : 0;
  GSize size = text_size(text, font);
  if (arrow_w + size.w > LAYOUT.windnow.w - 6) {
    snprintf(text, sizeof(text), "%d", hour.wind_speed);
    size = text_size(text, font);
  }
  int x = LAYOUT.windnow.x + (LAYOUT.windnow.w - arrow_w - size.w) / 2;
  if (arrow) {
    graphics_context_set_antialiased(ctx, true);
    graphics_context_set_stroke_width(ctx, 1);
    bool strong = hour.wind_h * 5 >= LAYOUT.wind.h * 3;
    graphics_context_set_stroke_color(ctx, strong ? t->wind_strong : t->muted);
    draw_wind_arrow(ctx, x + 4, LAYOUT.windnow.y + LAYOUT.windnow.h / 2 + 1, hour.wind_dir * 2, 9);
  }
  draw_text(ctx, text, font,
            GRect(x + arrow_w, LAYOUT.windnow.y - 1, size.w + 2, LAYOUT.windnow.h + 2),
            GTextAlignmentLeft, t->muted);
}

static void draw_steps(GContext *ctx, const Theme *t) {
  time_t start = time_start_of_today();
  time_t end = time(NULL);
  if (end <= start) end = start + 1;
  HealthServiceAccessibilityMask mask =
      health_service_metric_accessible(HealthMetricStepCount, start, end);
  if ((mask & HealthServiceAccessibilityMaskAvailable) == 0) return;
  HealthValue steps = health_service_sum_today(HealthMetricStepCount);
  if (steps < 0) return;
  char number[16];
  char text[24];
  format_grouped(number, (int)steps);
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  // Say what the number is when there is room for it.
  snprintf(text, sizeof(text), "%s steps", number);
  if (text_size(text, font).w > LAYOUT.steps.w) snprintf(text, sizeof(text), "%s", number);
  draw_fitted(ctx, text, font,
              GRect(LAYOUT.steps.x, LAYOUT.steps.y, LAYOUT.steps.w, LAYOUT.steps.h),
              GTextAlignmentCenter, t->muted);
}

#define BATTERY_W 17
#define BATTERY_H 9

static void draw_battery(GContext *ctx, const Theme *t) {
  BatteryChargeState charge = battery_state_service_peek();
  bool low = charge.charge_percent <= 20 && !charge.is_charging;
  GColor color = low ? GColorRed : t->muted;
  GColor level = charge.is_charging ? GColorIslamicGreen : (low ? GColorRed : t->text);
  char text[8];
  snprintf(text, sizeof(text), "%u%%", (unsigned)charge.charge_percent);
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  GSize size = text_size(text, font);
  int gap = 3;
  int total = BATTERY_W + 2 + gap + size.w;
  int x = LAYOUT.battery.x + (LAYOUT.battery.w - total) / 2;
  int y = LAYOUT.battery.y + (LAYOUT.battery.h - BATTERY_H) / 2 + 1;
  graphics_context_set_antialiased(ctx, false);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_stroke_color(ctx, color);
  graphics_draw_rect(ctx, GRect(x, y, BATTERY_W, BATTERY_H));
  fill_rect(ctx, x + BATTERY_W, y + 2, 2, BATTERY_H - 4, color);
  int fill = ((BATTERY_W - 4) * charge.charge_percent + 50) / 100;
  if (fill < 1) fill = 1;
  fill_rect(ctx, x + 2, y + 2, fill, BATTERY_H - 4, level);
  draw_text(ctx, text, font,
            GRect(x + BATTERY_W + 2 + gap, LAYOUT.battery.y - 1, size.w, LAYOUT.battery.h),
            GTextAlignmentLeft, color);
}

// Nothing shows while the phone is connected; a red Bluetooth rune shows when it is not.
static void draw_bluetooth(GContext *ctx) {
  if (bluetooth_connection_service_peek()) return;
  int x = LAYOUT.bt_x;
  int y = LAYOUT.bt_y;
  int h = LAYOUT.bt_r + 2;
  graphics_context_set_antialiased(ctx, false);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_stroke_color(ctx, GColorRed);
  graphics_draw_line(ctx, GPoint(x, y - h), GPoint(x, y + h));
  graphics_draw_line(ctx, GPoint(x, y - h), GPoint(x + h / 2 + 1, y - h / 2));
  graphics_draw_line(ctx, GPoint(x + h / 2 + 1, y - h / 2), GPoint(x - h / 2 - 1, y + h / 2));
  graphics_draw_line(ctx, GPoint(x, y + h), GPoint(x + h / 2 + 1, y + h / 2));
  graphics_draw_line(ctx, GPoint(x + h / 2 + 1, y + h / 2), GPoint(x - h / 2 - 1, y - h / 2));
}

void face_paint(Layer *layer, GContext *ctx) {
  Theme t = theme();
  GRect bounds = layer_get_bounds(layer);
  fill_rect(ctx, bounds.origin.x, bounds.origin.y, bounds.size.w, bounds.size.h, t.bg);
  time_t now = time(NULL);
  struct tm local = *localtime(&now);
  if (s_face.ok) {
    draw_chart(ctx, &t, now);
  } else {
    draw_waiting(ctx, &t);
  }
  draw_time(ctx, &t, &local);
  draw_meta(ctx, &t, &local, now);
  draw_steps(ctx, &t);
  draw_battery(ctx, &t);
  draw_wind_now(ctx, &t, now);
  draw_bluetooth(ctx);
}
