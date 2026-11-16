#include "face.h"
#include "layout.h"

#define FACE_MAX_HOURS 144
#define FACE_MAX_DAYS 6
#define FACE_MAX_SUNS 6
#define FACE_MAX_BYTES (40 + FACE_MAX_HOURS * 5 + FACE_MAX_DAYS * 8 + FACE_MAX_SUNS * 4)
#define FACE_CHUNK 240
#define FACE_LEN_KEY 1
#define FACE_DATA_KEY 2

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
  uint8_t x;
  uint8_t y;
  uint8_t r;
} FaceSun;

typedef struct {
  bool ok;
  uint8_t hour_count;
  uint8_t day_count;
  uint8_t sun_count;
  uint32_t fetched_at;
  uint32_t window_start;
  uint32_t window_end;
  int32_t utc_offset;
  int16_t current_deg;
  char place[16];
  FaceHour hours[FACE_MAX_HOURS];
  FaceDay days[FACE_MAX_DAYS];
  FaceSun suns[FACE_MAX_SUNS];
} Face;

static Face s_face;
static Face s_scratch;
static uint8_t s_blob[FACE_MAX_BYTES];

static const char *WEEKDAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

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

static bool unpack(Face *face, const uint8_t *data, int len) {
  memset(face, 0, sizeof(*face));
  if (!data || len < 40 || len > FACE_MAX_BYTES) return false;
  if (data[0] != 1 || data[1] != FACE_PLATFORM) return false;
  uint8_t hour_count = data[3];
  uint8_t day_count = data[22];
  uint8_t sun_count = data[23];
  int expect = 40 + (int)hour_count * 5 + (int)day_count * 8 + (int)sun_count * 4;
  if (expect != len) return false;
  if (hour_count > FACE_MAX_HOURS || day_count > FACE_MAX_DAYS || sun_count > FACE_MAX_SUNS) {
    return false;
  }

  face->ok = true;
  face->hour_count = hour_count;
  face->day_count = day_count;
  face->sun_count = sun_count;
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
    p += 5;
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
  for (i = 0; i < sun_count; i++) {
    face->suns[i].x = p[0];
    face->suns[i].y = p[1];
    face->suns[i].r = p[2];
    p += 4;
  }
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

static void vline(GContext *ctx, int x, int y, int h, GColor color) {
  if (h <= 0) return;
  graphics_context_set_antialiased(ctx, false);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_stroke_color(ctx, color);
  graphics_draw_line(ctx, GPoint(x, y), GPoint(x, y + h - 1));
}

static int local_seconds(int hour_index) {
  return (int)(((int64_t)s_face.window_start + (int64_t)hour_index * 3600 + s_face.utc_offset) % 86400);
}

static void draw_suns(GContext *ctx) {
  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_fill_color(ctx, GColorYellow);
  graphics_context_set_stroke_color(ctx, GColorYellow);
  int i;
  for (i = 0; i < s_face.sun_count; i++) {
    int r = s_face.suns[i].r;
    int cx = LAYOUT.cloud.x + s_face.suns[i].x;
    int cy = LAYOUT.cloud.y + s_face.suns[i].y;
    if (r <= 0) continue;
    // Rays stop at the edge of the cloud lane so they never reach the date row.
    int ray = LAYOUT.cloud.h / 2 - r;
    if (ray > r) ray = r;
    graphics_fill_circle(ctx, GPoint(cx, cy), (uint16_t)r);
    if (ray < 2) continue;
    int a = r + 1;
    int b = r + ray;
    graphics_draw_line(ctx, GPoint(cx, cy - a), GPoint(cx, cy - b));
    graphics_draw_line(ctx, GPoint(cx, cy + a), GPoint(cx, cy + b));
    graphics_draw_line(ctx, GPoint(cx - a, cy), GPoint(cx - b, cy));
    graphics_draw_line(ctx, GPoint(cx + a, cy), GPoint(cx + b, cy));
  }
}

static void draw_temperature(GContext *ctx) {
  int n = s_face.hour_count;
  if (n < 2) return;
  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 2);
  int plot_w = LAYOUT.plot.w;
  int i;
  for (i = 0; i < n - 1; i++) {
    int x0 = LAYOUT.plot.x + column_x(i, n, plot_w);
    int w0 = column_w(i, n, plot_w);
    int x1 = LAYOUT.plot.x + column_x(i + 1, n, plot_w);
    int w1 = column_w(i + 1, n, plot_w);
    graphics_draw_line(ctx,
                       GPoint(x0 + w0 / 2, LAYOUT.plot.y + s_face.hours[i].temp_y),
                       GPoint(x1 + w1 / 2, LAYOUT.plot.y + s_face.hours[i + 1].temp_y));
  }
  graphics_context_set_stroke_width(ctx, 1);
}

static void draw_now(GContext *ctx, time_t now) {
  if (s_face.window_end <= s_face.window_start) return;
  int64_t now64 = (int64_t)now;
  if (now64 < (int64_t)s_face.window_start || now64 >= (int64_t)s_face.window_end) return;
  int64_t elapsed = now64 - (int64_t)s_face.window_start;
  int64_t span = (int64_t)s_face.window_end - (int64_t)s_face.window_start;
  int x = LAYOUT.plot.x + (int)((elapsed * LAYOUT.plot.w) / span);
  fill_rect(ctx, x, LAYOUT.plot.y, 2, LAYOUT.plot.h, GColorSunsetOrange);

  // A dot marks where the temperature line is now, between the two nearest hour centers.
  int n = s_face.hour_count;
  if (n < 1) return;
  int px = x - LAYOUT.plot.x;
  int i = 0;
  while (i < n - 1) {
    int next_mid = column_x(i + 1, n, LAYOUT.plot.w) + column_w(i + 1, n, LAYOUT.plot.w) / 2;
    if (px < next_mid) break;
    i += 1;
  }
  int mid = column_x(i, n, LAYOUT.plot.w) + column_w(i, n, LAYOUT.plot.w) / 2;
  int y = s_face.hours[i].temp_y;
  if (i < n - 1 && px > mid) {
    int next_mid = column_x(i + 1, n, LAYOUT.plot.w) + column_w(i + 1, n, LAYOUT.plot.w) / 2;
    int dy = s_face.hours[i + 1].temp_y - y;
    if (next_mid > mid) y += (dy * (px - mid)) / (next_mid - mid);
  }
  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, GPoint(x, LAYOUT.plot.y + y), 5);
  graphics_context_set_fill_color(ctx, GColorSunsetOrange);
  graphics_fill_circle(ctx, GPoint(x, LAYOUT.plot.y + y), 3);
}

static GSize text_size(const char *text, GFont font) {
  return graphics_text_layout_get_content_size(
      text, font, GRect(0, 0, 120, 30), GTextOverflowModeWordWrap, GTextAlignmentLeft);
}

// A one-pixel black outline keeps labels readable over the line and the night shading.
static void draw_shadow_text(GContext *ctx, const char *text, GFont font, int x, int y, GSize size) {
  static const int8_t OFFSETS[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
  int k;
  graphics_context_set_text_color(ctx, GColorBlack);
  for (k = 0; k < 4; k++) {
    graphics_draw_text(ctx, text, font,
                       GRect(x + OFFSETS[k][0], y + OFFSETS[k][1], size.w, size.h),
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  }
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, text, font, GRect(x, y, size.w, size.h),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);
}

static void draw_day_labels(GContext *ctx) {
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  bool named = s_face.day_count > 1;
  int i;
  for (i = 0; i < s_face.day_count; i++) {
    FaceDay day = s_face.days[i];
    char max_text[12];
    char min_text[8];
    int span = day.span - 2;
    // With several days on screen, name each one; drop the name if the column is too narrow.
    const char *wday = (named && day.wday <= 6) ? WEEKDAYS[day.wday] : NULL;
    GSize max_size = GSizeZero;
    if (wday) {
      snprintf(max_text, sizeof(max_text), "%s %d", wday, day.max_deg);
      max_size = text_size(max_text, font);
    }
    if (!wday || max_size.w > span) {
      snprintf(max_text, sizeof(max_text), "%d", day.max_deg);
      max_size = text_size(max_text, font);
    }
    snprintf(min_text, sizeof(min_text), "%d", day.min_deg);
    GSize min_size = text_size(min_text, font);
    int x = LAYOUT.plot.x + day.x + 2;
    if (max_size.w > 0 && max_size.w <= span) {
      draw_shadow_text(ctx, max_text, font, x, LAYOUT.plot.y - 3, max_size);
    }
    if (min_size.w > 0 && min_size.w <= span) {
      int y = LAYOUT.plot.y + LAYOUT.plot.h - min_size.h;
      draw_shadow_text(ctx, min_text, font, x, y, min_size);
    }
  }
}

static void draw_hour_labels(GContext *ctx) {
#if defined(PBL_ROUND)
  (void)ctx;
  return;
#else
  if (s_face.hour_count == 0 || s_face.hour_count > 24) return;
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  bool h24 = clock_is_24h_style();
  int n = s_face.hour_count;
  int plot_w = LAYOUT.plot.w;
  int i;
  for (i = 0; i < n; i++) {
    int seconds = local_seconds(i);
    if (seconds < 0) continue;
    int hour = seconds / 3600;
    if (hour != 6 && hour != 12 && hour != 18) continue;
    char text[6];
    if (h24) {
      snprintf(text, sizeof(text), "%d", hour);
    } else {
      snprintf(text, sizeof(text), "%d%s", hour == 12 ? 12 : hour % 12, hour < 12 ? "a" : "p");
    }
    GSize size = graphics_text_layout_get_content_size(
        text, font, GRect(0, 0, 40, 24), GTextOverflowModeWordWrap, GTextAlignmentLeft);
    int col_x = LAYOUT.plot.x + column_x(i, n, plot_w);
    int col_w = column_w(i, n, plot_w);
    int x = col_x + col_w / 2 - size.w / 2;
    int y = LAYOUT.plot.y + LAYOUT.plot.h - size.h;
    draw_shadow_text(ctx, text, font, x, y, size);
  }
#endif
}

static void draw_chart(GContext *ctx, time_t now) {
  int n = s_face.hour_count;
  int plot_w = LAYOUT.plot.w;
  int i;
  graphics_context_set_antialiased(ctx, false);
  for (i = 0; i < n; i++) {
    if (!s_face.hours[i].night) continue;
    int x = LAYOUT.plot.x + column_x(i, n, plot_w);
    int w = column_w(i, n, plot_w);
    fill_rect(ctx, x, LAYOUT.plot.y, w, LAYOUT.plot.h, GColorOxfordBlue);
  }
  for (i = 0; i < n; i++) {
    int seconds = local_seconds(i);
    if (seconds != 0) continue;
    int x = LAYOUT.plot.x + column_x(i, n, plot_w);
    vline(ctx, x, LAYOUT.plot.y, LAYOUT.plot.h, GColorLightGray);
  }
  // Cloud cover grows out from the middle of its lane, so a thicker band reads as more cloud.
  for (i = 0; i < n; i++) {
    int h = s_face.hours[i].cloud_h;
    if (h <= 0) continue;
    int x = LAYOUT.plot.x + column_x(i, n, plot_w);
    int w = column_w(i, n, plot_w);
    int y = LAYOUT.cloud.y + (LAYOUT.cloud.h - h) / 2;
    fill_rect(ctx, x, y, w, h, h * 2 > LAYOUT.cloud.h ? GColorLightGray : GColorDarkGray);
  }
  fill_rect(ctx, LAYOUT.precip.x, LAYOUT.precip.y + LAYOUT.precip.h - 1, LAYOUT.precip.w, 1,
            GColorDarkGray);
  draw_suns(ctx);
  draw_temperature(ctx);
  draw_now(ctx, now);
  draw_day_labels(ctx);
  draw_hour_labels(ctx);
  for (i = 0; i < n; i++) {
    int x = LAYOUT.plot.x + column_x(i, n, plot_w);
    int w = column_w(i, n, plot_w);
    int rain = s_face.hours[i].rain_h;
    int snow = s_face.hours[i].snow_h;
    if (rain > 0) {
      fill_rect(ctx, x, LAYOUT.precip.y + LAYOUT.precip.h - rain, w, rain, GColorVividCerulean);
    }
    if (snow > 0) {
      fill_rect(ctx, x, LAYOUT.precip.y, w, snow, GColorWhite);
    }
  }
}

static void draw_fitted(GContext *ctx, const char *text, GFont font, GRect box,
                        GTextAlignment align, GColor color);

static void draw_waiting(GContext *ctx) {
  GRect box = GRect(LAYOUT.plot.x, LAYOUT.plot.y, LAYOUT.plot.w, LAYOUT.plot.h);
  graphics_context_set_text_color(ctx, GColorLightGray);
  graphics_draw_text(ctx, "Waiting for the forecast from your phone",
                     fonts_get_system_font(FONT_KEY_GOTHIC_18),
                     GRect(box.origin.x + 16, box.origin.y + box.size.h / 2 - 22, box.size.w - 32, 44),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void draw_fitted(GContext *ctx, const char *text, GFont font, GRect box,
                        GTextAlignment align, GColor color) {
  if (!text || !text[0] || box.size.w <= 0 || box.size.h <= 0) return;
  GSize size = graphics_text_layout_get_content_size(
      text, font, GRect(0, 0, 400, box.size.h), GTextOverflowModeWordWrap, GTextAlignmentLeft);
  if (size.h > 0 && size.h < box.size.h) {
    box.origin.y += (box.size.h - size.h) / 2;
    box.size.h = size.h;
  }
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, box, GTextOverflowModeTrailingEllipsis, align, NULL);
}

static void draw_time(GContext *ctx, struct tm *local) {
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
  GSize time_size = graphics_text_layout_get_content_size(
      text, time_font, GRect(0, 0, 220, 60), GTextOverflowModeWordWrap, GTextAlignmentLeft);
  GSize am_size = GSizeZero;
  if (ampm) {
    am_size = graphics_text_layout_get_content_size(
        ampm, small, GRect(0, 0, 40, 24), GTextOverflowModeWordWrap, GTextAlignmentLeft);
  }
  int gap = ampm ? 2 : 0;
  int x = LAYOUT.time.x;
#if defined(PBL_ROUND)
  x = LAYOUT.time.x + (LAYOUT.time.w - (time_size.w + gap + am_size.w)) / 2;
#endif
  int y = LAYOUT.time.y;
  if (time_size.h > 0 && time_size.h < LAYOUT.time.h) y += (LAYOUT.time.h - time_size.h) / 2;
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, text, time_font, GRect(x, y, time_size.w, time_size.h),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  if (ampm) {
    int ay = y + time_size.h - am_size.h;
    graphics_draw_text(ctx, ampm, small, GRect(x + time_size.w + gap, ay, am_size.w, am_size.h),
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  }
}

static bool forecast_stale(time_t now) {
  int64_t now64 = (int64_t)now;
  int64_t fetched = (int64_t)s_face.fetched_at;
  if (now64 <= fetched) return false;
  return (now64 - fetched) > 7200;
}

static void draw_temp(GContext *ctx, time_t now) {
  char text[16];
  // An old forecast turns the temperature gray instead of hiding it.
  GColor color = GColorWhite;
  if (!s_face.ok || s_face.current_deg == -32768) {
    snprintf(text, sizeof(text), "--\xc2\xb0");
    color = GColorLightGray;
  } else {
    snprintf(text, sizeof(text), "%d\xc2\xb0", s_face.current_deg);
    if (forecast_stale(now)) color = GColorLightGray;
  }
#if defined(PBL_ROUND)
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GTextAlignment align = GTextAlignmentCenter;
#else
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  GTextAlignment align = GTextAlignmentRight;
#endif
  draw_fitted(ctx, text, font, GRect(LAYOUT.temp.x, LAYOUT.temp.y, LAYOUT.temp.w, LAYOUT.temp.h),
              align, color);
}

static void draw_date_place(GContext *ctx, struct tm *local) {
  const char *wday = (local->tm_wday >= 0 && local->tm_wday <= 6) ? WEEKDAYS[local->tm_wday] : "";
  char date[12];
  snprintf(date, sizeof(date), "%s %d", wday, local->tm_mday);
#if defined(PBL_ROUND)
  char meta[40];
  if (s_face.ok && s_face.place[0]) {
    snprintf(meta, sizeof(meta), "%s  %s", date, s_face.place);
  } else {
    snprintf(meta, sizeof(meta), "%s", date);
  }
  draw_fitted(ctx, meta, fonts_get_system_font(FONT_KEY_GOTHIC_14),
              GRect(LAYOUT.meta.x, LAYOUT.meta.y, LAYOUT.meta.w, LAYOUT.meta.h),
              GTextAlignmentCenter, GColorWhite);
#else
  draw_fitted(ctx, date, fonts_get_system_font(FONT_KEY_GOTHIC_18),
              GRect(LAYOUT.date.x, LAYOUT.date.y, LAYOUT.date.w, LAYOUT.date.h),
              GTextAlignmentLeft, GColorWhite);
  if (s_face.ok && s_face.place[0]) {
    draw_fitted(ctx, s_face.place, fonts_get_system_font(FONT_KEY_GOTHIC_18),
                GRect(LAYOUT.place.x, LAYOUT.place.y, LAYOUT.place.w, LAYOUT.place.h),
                GTextAlignmentRight, GColorWhite);
  }
#endif
}

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

static void draw_steps(GContext *ctx) {
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
#if defined(PBL_ROUND)
  GTextAlignment align = GTextAlignmentCenter;
#else
  GTextAlignment align = GTextAlignmentLeft;
#endif
  draw_fitted(ctx, text, font,
              GRect(LAYOUT.steps.x, LAYOUT.steps.y, LAYOUT.steps.w, LAYOUT.steps.h),
              align, GColorLightGray);
}

#define BATTERY_W 17
#define BATTERY_H 9

static void draw_battery(GContext *ctx) {
  BatteryChargeState charge = battery_state_service_peek();
  bool low = charge.charge_percent <= 20 && !charge.is_charging;
  GColor color = low ? GColorRed : GColorLightGray;
  GColor level = charge.is_charging ? GColorGreen : (low ? GColorRed : GColorWhite);
  char text[8];
  snprintf(text, sizeof(text), "%u%%", (unsigned)charge.charge_percent);
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  GSize size = text_size(text, font);
  int gap = 3;
  int total = BATTERY_W + 2 + gap + size.w;
#if defined(PBL_ROUND)
  int x = LAYOUT.battery.x + (LAYOUT.battery.w - total) / 2;
#else
  int x = LAYOUT.battery.x + LAYOUT.battery.w - total;
#endif
  int y = LAYOUT.battery.y + (LAYOUT.battery.h - BATTERY_H) / 2 + 1;
  graphics_context_set_antialiased(ctx, false);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_stroke_color(ctx, color);
  graphics_draw_rect(ctx, GRect(x, y, BATTERY_W, BATTERY_H));
  fill_rect(ctx, x + BATTERY_W, y + 2, 2, BATTERY_H - 4, color);
  int fill = ((BATTERY_W - 4) * charge.charge_percent + 50) / 100;
  if (fill < 1) fill = 1;
  fill_rect(ctx, x + 2, y + 2, fill, BATTERY_H - 4, level);
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font,
                     GRect(x + BATTERY_W + 2 + gap, LAYOUT.battery.y - 1, size.w, LAYOUT.battery.h),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);
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
  GRect bounds = layer_get_bounds(layer);
  fill_rect(ctx, bounds.origin.x, bounds.origin.y, bounds.size.w, bounds.size.h, GColorBlack);
  time_t now = time(NULL);
  struct tm local = *localtime(&now);
  if (s_face.ok) {
    draw_chart(ctx, now);
  } else {
    draw_waiting(ctx);
  }
  draw_time(ctx, &local);
  draw_temp(ctx, now);
  draw_date_place(ctx, &local);
  draw_steps(ctx);
  draw_battery(ctx);
  draw_bluetooth(ctx);
}
