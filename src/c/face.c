#include "face.h"
#include "layout.h"

#define FACE_VERSION 2
#define FACE_HOUR_BYTES 6
#define FACE_MAX_HOURS 144
#define FACE_MAX_DAYS 6
#define FACE_MAX_SUNS 6
#define FACE_MAX_BYTES (40 + FACE_MAX_HOURS * FACE_HOUR_BYTES + FACE_MAX_DAYS * 8 + FACE_MAX_SUNS * 4)
#define FACE_CHUNK 240
#define FACE_LEN_KEY 1
#define FACE_DATA_KEY 2
#define FACE_NO_ROW 255

// The unit byte in the frame header carries this flag when the light theme is chosen.
#define FACE_LIGHT_FLAG 0x10

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
  GColor line;
  GColor grid;
  GColor warmer;
  GColor colder;
  GColor rain;
  GColor snow;
  GColor now;
  GColor sky_sun;
  GColor sky_cloud;
  GColor sky_night;
} Theme;

static Face s_face;
static Face s_scratch;
static uint8_t s_blob[FACE_MAX_BYTES];

static const char *WEEKDAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static Theme theme(void) {
  Theme t;
  // Until the first forecast arrives, the face is dark.
  if (s_face.light) {
    t.bg = GColorWhite;
    t.text = GColorBlack;
    t.muted = GColorDarkGray;
    t.line = GColorBlack;
    t.grid = GColorLightGray;
    t.warmer = GColorOrange;
    t.colder = GColorTiffanyBlue;
    t.rain = GColorCobaltBlue;
    t.snow = GColorDarkGray;
    t.now = GColorBlack;
    t.sky_sun = GColorChromeYellow;
    t.sky_cloud = GColorLightGray;
    t.sky_night = GColorCeleste;
  } else {
    t.bg = GColorBlack;
    t.text = GColorWhite;
    t.muted = GColorLightGray;
    t.line = GColorWhite;
    t.grid = GColorDarkGray;
    t.warmer = GColorChromeYellow;
    t.colder = GColorTiffanyBlue;
    t.rain = GColorPictonBlue;
    t.snow = GColorWhite;
    t.now = GColorWhite;
    t.sky_sun = GColorYellow;
    t.sky_cloud = GColorDarkGray;
    t.sky_night = GColorOxfordBlue;
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
  int expect = 40 + (int)hour_count * FACE_HOUR_BYTES + (int)day_count * 8 + (int)sun_count * 4;
  if (expect != len) return false;
  if (hour_count > FACE_MAX_HOURS || day_count > FACE_MAX_DAYS || sun_count > FACE_MAX_SUNS) {
    return false;
  }

  face->ok = true;
  face->light = (data[2] & FACE_LIGHT_FLAG) != 0;
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
    p += FACE_HOUR_BYTES;
  }
  for (i = 0; i < day_count; i++) {
    face->days[i].x = p[0];
    face->days[i].span = p[1];
    face->days[i].max_deg = read_i16(p + 2);
    face->days[i].min_deg = read_i16(p + 4);
    face->days[i].wday = p[6];
    face->days[i].mday = p[7];
    p += 8;
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
  return s_face.hours[i].night ? t->sky_night : t->bg;
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
    graphics_context_set_fill_color(ctx, t->sky_night);
    graphics_fill_rect(ctx, GRect(LAYOUT.plot.x + x0, top, x1 - x0, bottom - top), 3,
                       GCornersAll);
    i = j;
  }
}

// The sky lane: cloud is a band that swells with cover and has rounded ends, and a sun
// sits in the middle of each clear daytime stretch.
static void draw_sky(GContext *ctx, const Theme *t) {
  int n = s_face.hour_count;
  int w = LAYOUT.plot.w;
  int lane = LAYOUT.cloud.h;
  int cy = LAYOUT.cloud.y + lane / 2;
  int max_r = lane / 2;
  int i;
  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_fill_color(ctx, t->sky_cloud);
  for (i = 0; i < n; i++) {
    int r = (s_face.hours[i].cloud_h * max_r + lane / 2) / lane;
    if (r < 1) continue;
    fill_rect(ctx, LAYOUT.plot.x + column_x(i, n, w), cy - r, column_w(i, n, w), 2 * r, t->sky_cloud);
    graphics_context_set_fill_color(ctx, t->sky_cloud);
    graphics_fill_circle(ctx, GPoint(LAYOUT.plot.x + column_mid(i, n, w), cy), (uint16_t)r);
  }
  i = 0;
  while (i < n) {
    if (s_face.hours[i].night || s_face.hours[i].cloud_h * 4 > lane) {
      i += 1;
      continue;
    }
    int j = i;
    while (j < n && !s_face.hours[j].night && s_face.hours[j].cloud_h * 4 <= lane) j += 1;
    int x0 = column_x(i, n, w);
    int x1 = column_x(j, n, w);
    if (x1 - x0 >= 10) {
      graphics_context_set_fill_color(ctx, t->sky_sun);
      graphics_fill_circle(ctx, GPoint(LAYOUT.plot.x + (x0 + x1) / 2, cy), (uint16_t)(max_r - 1));
    }
    i = j;
  }
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
    if (prev_y > now_y + 1) {
      graphics_context_set_stroke_color(ctx, t->warmer);
      y0 = now_y;
      y1 = prev_y;
    } else if (now_y > prev_y + 1) {
      graphics_context_set_stroke_color(ctx, t->colder);
      y0 = prev_y;
      y1 = now_y;
    } else {
      continue;
    }
    // A checkerboard reads as a softer tone than a solid fill, so the line stays on top.
    int y;
    for (y = y0 + ((y0 + px) & 1); y < y1; y += 2) {
      graphics_draw_pixel(ctx, GPoint(x, LAYOUT.plot.y + y));
    }
  }
}

static void draw_line(GContext *ctx, const Theme *t) {
  int n = s_face.hour_count;
  int w = LAYOUT.plot.w;
  int i;
  if (n < 2) return;
  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_stroke_color(ctx, t->line);
  graphics_context_set_stroke_width(ctx, 2);
  for (i = 0; i < n - 1; i++) {
    graphics_draw_line(ctx,
                       GPoint(LAYOUT.plot.x + column_mid(i, n, w), LAYOUT.plot.y + s_face.hours[i].temp_y),
                       GPoint(LAYOUT.plot.x + column_mid(i + 1, n, w), LAYOUT.plot.y + s_face.hours[i + 1].temp_y));
  }
  graphics_context_set_stroke_width(ctx, 1);
}

static void draw_now(GContext *ctx, const Theme *t, time_t now) {
  if (s_face.window_end <= s_face.window_start) return;
  int64_t now64 = (int64_t)now;
  if (now64 < (int64_t)s_face.window_start || now64 >= (int64_t)s_face.window_end) return;
  int64_t span = (int64_t)s_face.window_end - (int64_t)s_face.window_start;
  int px = (int)(((now64 - (int64_t)s_face.window_start) * LAYOUT.plot.w) / span);
  int x = LAYOUT.plot.x + px;
  int y0 = LAYOUT.cloud.y + LAYOUT.cloud.h;
  int yy;
  for (yy = y0; yy < LAYOUT.precip.y + LAYOUT.precip.h; yy += 2) fill_rect(ctx, x, yy, 1, 1, t->now);
  bool known;
  int y = LAYOUT.plot.y + row_at(px, false, &known);
  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_fill_color(ctx, t->now);
  graphics_fill_circle(ctx, GPoint(x, y), 5);
  graphics_context_set_fill_color(ctx, t->bg);
  graphics_fill_circle(ctx, GPoint(x, y), 2);
}

static void draw_day_labels(GContext *ctx, const Theme *t) {
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  bool named = s_face.day_count > 1;
  int i;
  for (i = 0; i < s_face.day_count; i++) {
    FaceDay day = s_face.days[i];
    char name_text[6] = "";
    char max_text[8];
    char min_text[8];
    int span = day.span - 3;
    snprintf(max_text, sizeof(max_text), "%d", day.max_deg);
    snprintf(min_text, sizeof(min_text), "%d", day.min_deg);
    GSize max_size = text_size(max_text, font);
    GSize min_size = text_size(min_text, font);
    // With several days on screen, name each one in a quieter color; drop the name
    // if the column is too narrow for it.
    GSize name_size = GSizeZero;
    int gap = 2;
    if (named && day.wday <= 6) {
      snprintf(name_text, sizeof(name_text), "%s", WEEKDAYS[day.wday]);
      name_size = text_size(name_text, font);
      if (name_size.w + gap + max_size.w > span) name_size = GSizeZero;
    }
    int x = LAYOUT.plot.x + day.x + 3;
    int top = LAYOUT.plot.y - 3;
    int max_x = name_size.w > 0 ? x + name_size.w + gap : x;
    if (name_size.w > 0) {
      draw_halo_text(ctx, t, name_text, font, x, top, name_size, t->muted);
    }
    if (max_size.w > 0 && max_x + max_size.w <= x + span) {
      draw_halo_text(ctx, t, max_text, font, max_x, top, max_size, t->text);
    }
    if (min_size.w > 0 && min_size.w <= span) {
      draw_halo_text(ctx, t, min_text, font, x, LAYOUT.plot.y + LAYOUT.plot.h - min_size.h, min_size,
                     t->text);
    }
  }
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
    int rain = s_face.hours[i].rain_h;
    int snow = s_face.hours[i].snow_h;
    if (rain > 0) fill_rect(ctx, x, base - rain, cw, rain, t->rain);
    if (snow > 0) {
      int top = base - rain;
      int y;
      for (y = top - 2; y > top - snow - 1 && y >= LAYOUT.precip.y; y -= 3) {
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
  GFont time_font = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
  GFont small = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  GSize time_size = text_size(text, time_font);
  GSize am_size = ampm ? text_size(ampm, small) : GSizeZero;
  int gap = ampm ? 2 : 0;
  int x = LAYOUT.time.x + (LAYOUT.time.w - (time_size.w + gap + am_size.w)) / 2;
  int y = LAYOUT.time.y;
  if (time_size.h > 0 && time_size.h < LAYOUT.time.h) y += (LAYOUT.time.h - time_size.h) / 2;
  draw_text(ctx, text, time_font, GRect(x, y, time_size.w, time_size.h), GTextAlignmentLeft, t->text);
  if (ampm) {
    draw_text(ctx, ampm, small,
              GRect(x + time_size.w + gap, y + time_size.h - am_size.h, am_size.w, am_size.h),
              GTextAlignmentLeft, t->text);
  }
}

static bool forecast_stale(time_t now) {
  int64_t now64 = (int64_t)now;
  int64_t fetched = (int64_t)s_face.fetched_at;
  if (now64 <= fetched) return false;
  return (now64 - fetched) > 7200;
}

static void draw_temp(GContext *ctx, const Theme *t, time_t now) {
  char text[16];
  // An old forecast turns the temperature gray instead of hiding it.
  GColor color = t->text;
  if (!s_face.ok || s_face.current_deg == -32768) {
    snprintf(text, sizeof(text), "--\xc2\xb0");
    color = t->muted;
  } else {
    snprintf(text, sizeof(text), "%d\xc2\xb0", s_face.current_deg);
    if (forecast_stale(now)) color = t->muted;
  }
  draw_fitted(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
              GRect(LAYOUT.temp.x, LAYOUT.temp.y, LAYOUT.temp.w, LAYOUT.temp.h),
              GTextAlignmentCenter, color);
}

static void draw_meta(GContext *ctx, const Theme *t, struct tm *local) {
  const char *wday = (local->tm_wday >= 0 && local->tm_wday <= 6) ? WEEKDAYS[local->tm_wday] : "";
  char meta[40];
  if (s_face.ok && s_face.place[0]) {
    snprintf(meta, sizeof(meta), "%s %d  \xc2\xb7  %s", wday, local->tm_mday, s_face.place);
  } else {
    snprintf(meta, sizeof(meta), "%s %d", wday, local->tm_mday);
  }
  draw_fitted(ctx, meta, fonts_get_system_font(FONT_KEY_GOTHIC_18),
              GRect(LAYOUT.meta.x, LAYOUT.meta.y, LAYOUT.meta.w, LAYOUT.meta.h),
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
  draw_temp(ctx, &t, now);
  draw_meta(ctx, &t, &local);
  draw_steps(ctx, &t);
  draw_battery(ctx, &t);
  draw_bluetooth(ctx);
}
