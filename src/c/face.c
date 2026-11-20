#include "face.h"
#include "layout.h"

#define FACE_VERSION 4
#define FACE_HOUR_BYTES 7
#define FACE_DAY_BYTES 8
#define FACE_MAX_HOURS 144
#define FACE_MAX_DAYS 6
#define FACE_CHUNK 240
#define FACE_LEN_KEY 1
#define FACE_DATA_KEY 2
// Frame chunks use keys from FACE_DATA_KEY upward. This sits clear of them.
#define FACE_TOP_KEY 100
#define FACE_NO_ROW 255
#define FACE_NO_DIR 15
#define FACE_NO_SPEED 255
#define FACE_NO_DEG -32768

// The unit byte in the frame header carries this flag when the light theme is chosen.
#define FACE_LIGHT_FLAG 0x10
// And this one when the mono theme is chosen: black, white and grays only.
#define FACE_MONO_FLAG 0x20
// And this one when the top line shows the last successful fetch instead of the wind.
#define FACE_UPDATED_FLAG 0x40

#if defined(PBL_ROUND)
#define FACE_PLATFORM 2
#define TIME_CLEAR_X (LAYOUT.time.x)
#define TIME_CLEAR_Y (LAYOUT.time.y)
#define TIME_CLEAR_W (LAYOUT.time.w)
#define TIME_CLEAR_H (LAYOUT.time.h + 2)
#else
#define FACE_PLATFORM 1
// Leaves x < 22 untouched so the Bluetooth rune on the left edge never overlaps the time clear.
#define TIME_CLEAR_X 22
#define TIME_CLEAR_Y (LAYOUT.time.y - 1)
#define TIME_CLEAR_W (200 - TIME_CLEAR_X)
#define TIME_CLEAR_H (LAYOUT.time.h + 2)
#endif

// Chart bounding box including the 5px radius of the now ring at the left/right edges
// and the 2px mono night bar beneath the precipitation lane.
#define CHART_X (LAYOUT.plot.x - 5)
#define CHART_Y (LAYOUT.cloud.y)
#define CHART_W (LAYOUT.plot.w + 10)
#define CHART_H (LAYOUT.precip.y + LAYOUT.precip.h + 2 - LAYOUT.cloud.y)

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
  bool top_updated;
  uint8_t hour_count;
  uint8_t day_count;
  uint8_t refresh_min;
  uint32_t fetched_at;
  uint32_t window_start;
  uint32_t window_end;
  int32_t utc_offset;
  int16_t current_deg;
  char place[16];
  FaceHour hours[FACE_MAX_HOURS];
  FaceDay days[FACE_MAX_DAYS];
} Face;

// Where one day's labels go: its name and high along the top of the chart, its low along the
// bottom. Precomputed when a frame arrives so paint never re-measures them.
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
  GColor wind_barb;
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
// Kept apart from the frame so it survives before the first forecast, and a restart.
static bool s_show_updated;
// The frame as stored on the watch, so a repeat of it skips the flash write.
static uint8_t s_blob[FACE_MAX_BYTES];
static int s_blob_len = 0;

// Precomputed per-frame geometry and labels, and an 8-bit cache of the static chart.
static int16_t s_col_mid[FACE_MAX_HOURS];
static DayLabel s_day_labels[FACE_MAX_DAYS];
static GBitmap *s_chart_cache;
static bool s_chart_cache_valid;

// Partial-redraw state so minute ticks only repaint regions that actually changed.
static uint8_t s_dirty = FACE_DIRTY_ALL;
static int s_last_now_px = -2;
static int s_last_temp_now = FACE_NO_DEG;
static bool s_last_temp_stale = false;
static int s_last_hour_now = -2;
static int s_last_steps = -2;
static bool s_health_checked = false;
static bool s_health_avail = false;
static uint8_t s_last_charge_pct = 255;
static bool s_last_charging = false;
static int8_t s_last_bt = -1;
static bool s_last_24h = false;

static const char *WEEKDAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// One palette per theme: grays for clouds and wind, a warm and a cool tone for warmer and
// colder than yesterday, blue for water, a quiet band for night, a ramp for the line that
// tints only toward hot and freezing, and one orange accent. The watch's reflective panel
// mutes color a lot (pastels land near white, oranges near peach), so every color here was
// picked by how it looks after the SDK's panel color correction, not by its nominal value.
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
    t.wind_barb = GColorWhite;
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
    t.colder = GColorBlueMoon;
    t.rain = GColorBlue;
    t.snow = GColorDarkGray;
    t.now = GColorBlack;
    t.sky_cloud = GColorLightGray;
    t.sky_night = GColorBabyBlueEyes;
    t.cloud_rim = GColorDarkGray;
    t.wind = GColorLightGray;
    t.wind_barb = GColorBlack;
    t.accent = GColorOrange;
    t.thermal[0] = GColorDukeBlue;
    t.thermal[1] = GColorBlueMoon;
    t.thermal[2] = GColorBlack;
    t.thermal[3] = GColorWindsorTan;
    t.thermal[4] = GColorRed;
  } else {
    t.bg = GColorBlack;
    t.text = GColorWhite;
    t.muted = GColorLightGray;
    t.grid = GColorDarkGray;
    t.warmer = GColorChromeYellow;
    t.colder = GColorPictonBlue;
    t.rain = GColorVividCerulean;
    t.snow = GColorWhite;
    t.now = GColorWhite;
    t.sky_cloud = GColorLightGray;
    t.sky_night = GColorOxfordBlue;
    t.cloud_rim = GColorWhite;
    t.wind = GColorDarkGray;
    t.wind_barb = GColorWhite;
    t.accent = GColorChromeYellow;
    // Mild is plain white. Pale tints wash out to white on the reflective panel, so each
    // other stop is a color that still reads as itself there.
    t.thermal[0] = GColorPictonBlue;
    t.thermal[1] = GColorCyan;
    t.thermal[2] = GColorWhite;
    t.thermal[3] = GColorChromeYellow;
    t.thermal[4] = GColorSunsetOrange;
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

static void prepare_frame(void);

static bool unpack(Face *face, const uint8_t *data, int len) {
  memset(face, 0, sizeof(*face));
  if (!data || len < 40 || len > FACE_MAX_BYTES) return false;
  if (data[0] != FACE_VERSION || data[1] != FACE_PLATFORM) return false;
  uint8_t hour_count = data[3];
  uint8_t day_count = data[22];
  uint8_t refresh_min = data[23];
  int expect = 40 + (int)hour_count * FACE_HOUR_BYTES + (int)day_count * FACE_DAY_BYTES;
  if (expect != len) return false;
  if (hour_count > FACE_MAX_HOURS || day_count > FACE_MAX_DAYS) {
    return false;
  }

  face->ok = true;
  face->light = (data[2] & FACE_LIGHT_FLAG) != 0;
  face->mono = !face->light && (data[2] & FACE_MONO_FLAG) != 0;
  face->metric = (data[2] & 0x0F) == 2;
  face->top_updated = (data[2] & FACE_UPDATED_FLAG) != 0;
  face->hour_count = hour_count;
  face->day_count = day_count;
  face->refresh_min = refresh_min ? refresh_min : 60;
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
    uint8_t precip = p[4];
    uint8_t sky = p[5];
    uint8_t wind = p[6];
    face->hours[i].temp_y = p[0];
    face->hours[i].prev_y = p[1];
    face->hours[i].deg = (int8_t)p[2];
    face->hours[i].wind_speed = p[3];
    face->hours[i].rain_h = precip & 0x0F;
    face->hours[i].snow_h = (precip >> 4) & 0x0F;
    face->hours[i].cloud_h = sky & 0x1F;
    face->hours[i].night = (sky >> 5) & 1;
    face->hours[i].wind_h = wind & 0x0F;
    face->hours[i].wind_dir = (wind >> 4) & 0x0F;
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

// Writes the choice only when it changes. The default (wind, nothing stored) stays unwritten
// so an upgrade does not touch the flash. Returns true if the visible choice changed.
static bool apply_top(bool updated) {
  bool changed = s_show_updated != updated;
  bool known = persist_exists(FACE_TOP_KEY);
  if ((!known && !updated) || (known && persist_read_bool(FACE_TOP_KEY) == updated)) {
    s_show_updated = updated;
    return changed;
  }
  s_show_updated = updated;
  persist_write_bool(FACE_TOP_KEY, updated);
  return changed;
}

bool face_has_frame(void) {
  return s_face.ok;
}

uint8_t face_set_frame(const uint8_t *data, int len) {
  if (s_face.ok && len == s_blob_len && memcmp(data, s_blob, (size_t)len) == 0) return 0;
  if (!unpack(&s_scratch, data, len)) return 0;
  bool full = !s_face.ok || s_scratch.light != s_face.light || s_scratch.mono != s_face.mono;
  bool top_changed = apply_top(s_scratch.top_updated);
  bool meta_changed = full || strncmp(s_face.place, s_scratch.place, sizeof(s_face.place)) != 0;
  bool top_center_changed = top_changed || !s_show_updated || s_scratch.fetched_at != s_face.fetched_at;
  s_face = s_scratch;
  prepare_frame();
  s_chart_cache_valid = false;
  save_bytes(data, len);
  memcpy(s_blob, data, (size_t)len);
  s_blob_len = len;
  if (full) return FACE_DIRTY_ALL;
  uint8_t mask = FACE_DIRTY_CHART;
  if (meta_changed) mask |= FACE_DIRTY_META;
  if (top_center_changed) mask |= FACE_DIRTY_TOP_CENTER;
  return mask;
}

bool face_set_top_line(bool updated) {
  return apply_top(updated);
}

void face_load(void) {
  if (persist_exists(FACE_TOP_KEY)) s_show_updated = persist_read_bool(FACE_TOP_KEY);
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
  s_blob_len = len;
  // A choice saved on its own, before any forecast, wins over a frame that predates it.
  if (!persist_exists(FACE_TOP_KEY)) s_show_updated = s_face.top_updated;
  prepare_frame();
  s_chart_cache_valid = false;
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
  if (cols <= 0) return;
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (fb) {
    GRect fb_bounds = gbitmap_get_bounds(fb);
    uint8_t color = t->sky_night.argb;
    int y;
    for (y = top; y < bottom; y++) {
      if (y < fb_bounds.origin.y || y >= fb_bounds.origin.y + fb_bounds.size.h) continue;
      GBitmapDataRowInfo info = gbitmap_get_data_row_info(fb, y);
      int k;
      for (k = 0; k < cols; k++) {
        int cx = x + dir * k;
        if (cx < info.min_x || cx > info.max_x) continue;
        bool on;
        if (k == 0) {
          on = ((cx | y) & 1) == 0;
        } else if (k == 1) {
          on = ((cx + y) & 1) == 0;
        } else {
          on = ((cx & y) & 1) == 0;
        }
        if (on) info.data[cx] = color;
      }
    }
    graphics_release_frame_buffer(ctx, fb);
    return;
  }
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

// Advances `*idx` monotonically as px walks left to right, giving O(w + n) interpolation.
static int field_at_step(int px, int *idx, const uint8_t *base, int stride) {
  int n = s_face.hour_count;
  int i = *idx;
  while (i < n - 1 && px >= s_col_mid[i + 1]) i += 1;
  *idx = i;
  int a = base[i * stride];
  int mid = s_col_mid[i];
  if (i >= n - 1 || px <= mid) return a;
  int b = base[(i + 1) * stride];
  int next = s_col_mid[i + 1];
  if (next <= mid) return a;
  return a + ((b - a) * (px - mid)) / (next - mid);
}

// Clouds are a bank of puffs sitting on the lane floor. Each puff grows with the cover
// under it, so thin cloud is a low ripple and overcast fills the lane. A one-pixel rim on
// top of each puff gives the bank some shape.
static void draw_clouds(GContext *ctx, const Theme *t) {
  if (s_face.hour_count == 0) return;
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
    int idx = 0;
    graphics_context_set_fill_color(ctx, pass == 0 ? t->cloud_rim : t->sky_cloud);
    for (px = 0; px < w; px += step, k++) {
      int c = field_at_step(px, &idx, &s_face.hours[0].cloud_h, sizeof(FaceHour));
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

// Row of the line at pixel column px, advancing `*idx` monotonically.
static int row_at_step(int px, int *idx, bool prev, bool *known) {
  int n = s_face.hour_count;
  int i = *idx;
  while (i < n - 1 && px >= s_col_mid[i + 1]) i += 1;
  *idx = i;
  int a = prev ? s_face.hours[i].prev_y : s_face.hours[i].temp_y;
  if (prev && a == FACE_NO_ROW) {
    *known = false;
    return 0;
  }
  *known = true;
  int mid = s_col_mid[i];
  if (i >= n - 1 || px <= mid) return a;
  int b = prev ? s_face.hours[i + 1].prev_y : s_face.hours[i + 1].temp_y;
  if (prev && b == FACE_NO_ROW) return a;
  int next = s_col_mid[i + 1];
  if (next <= mid) return a;
  return a + ((b - a) * (px - mid)) / (next - mid);
}

static int row_at(int px, bool prev, bool *known) {
  int idx = 0;
  return row_at_step(px, &idx, prev, known);
}

// Shade between today and yesterday at the same hour: warm where today is warmer, cool where colder.
static void draw_fronts(GContext *ctx, const Theme *t) {
  if (s_face.hour_count == 0) return;
  int px;
  int idx = 0;
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (fb) {
    GRect fb_bounds = gbitmap_get_bounds(fb);
    GBitmapDataRowInfo rows[80];
    int plot_h = LAYOUT.plot.h;
    if (plot_h > (int)ARRAY_LENGTH(rows)) plot_h = (int)ARRAY_LENGTH(rows);
    int ry;
    for (ry = 0; ry < plot_h; ry++) {
      int sy = LAYOUT.plot.y + ry;
      if (sy >= fb_bounds.origin.y && sy < fb_bounds.origin.y + fb_bounds.size.h) {
        rows[ry] = gbitmap_get_data_row_info(fb, sy);
      } else {
        rows[ry].data = NULL;
        rows[ry].min_x = 1;
        rows[ry].max_x = 0;
      }
    }
    for (px = 0; px < LAYOUT.plot.w; px++) {
      bool known_now;
      bool known_prev;
      int now_y = row_at_step(px, &idx, false, &known_now);
      int prev_y = row_at_step(px, &idx, true, &known_prev);
      if (!known_prev) continue;
      int x = LAYOUT.plot.x + px;
      int y0;
      int y1;
      bool striped = false;
      uint8_t color;
      if (prev_y > now_y + 1) {
        color = t->warmer.argb;
        y0 = now_y;
        y1 = prev_y;
      } else if (now_y > prev_y + 1) {
        color = t->colder.argb;
        y0 = prev_y;
        y1 = now_y;
        striped = t->striped_colder;
      } else {
        continue;
      }
      if (y0 < -1) y0 = -1;
      if (y1 > plot_h) y1 = plot_h;
      int y;
      if (striped) {
        for (y = y0 + 1; y < y1; y++) {
          int sy = LAYOUT.plot.y + y;
          if ((sy & 1) == 0 && rows[y].data && x >= rows[y].min_x && x <= rows[y].max_x) {
            rows[y].data[x] = color;
          }
        }
      } else {
        for (y = y0 + 1; y < y1; y++) {
          int sy = LAYOUT.plot.y + y;
          if ((x & sy & 1) == 0 && rows[y].data && x >= rows[y].min_x && x <= rows[y].max_x) {
            rows[y].data[x] = color;
          }
        }
      }
    }
    graphics_release_frame_buffer(ctx, fb);
    return;
  }

  graphics_context_set_antialiased(ctx, false);
  for (px = 0; px < LAYOUT.plot.w; px++) {
    bool known_now;
    bool known_prev;
    int now_y = row_at_step(px, &idx, false, &known_now);
    int prev_y = row_at_step(px, &idx, true, &known_prev);
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
    // Three pixels of every four: dense enough to hold its color on the reflective panel,
    // open enough that the line stays on top.
    int y;
    if (striped) {
      // Every other row, so colder reads as horizontal lines rather than a checker.
      for (y = y0 + 1; y < y1; y++) {
        if (((LAYOUT.plot.y + y) & 1) == 0) graphics_draw_pixel(ctx, GPoint(x, LAYOUT.plot.y + y));
      }
      continue;
    }
    for (y = y0 + 1; y < y1; y++) {
      int sy = LAYOUT.plot.y + y;
      if ((x & sy & 1) == 0) graphics_draw_pixel(ctx, GPoint(x, sy));
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
    graphics_context_set_stroke_width(ctx, band == 0 ? 2 : (band >= 3 ? 4 : 3));
  } else {
    graphics_context_set_stroke_width(ctx, 3);
  }
}

static void draw_line(GContext *ctx, const Theme *t) {
  int n = s_face.hour_count;
  int i;
  if (n < 2) return;
  graphics_context_set_antialiased(ctx, true);
  for (i = 0; i < n - 1; i++) {
    set_thermal_stroke(ctx, t, (s_face.hours[i].deg + s_face.hours[i + 1].deg) / 2);
    graphics_draw_line(ctx,
                       GPoint(LAYOUT.plot.x + s_col_mid[i], LAYOUT.plot.y + s_face.hours[i].temp_y),
                       GPoint(LAYOUT.plot.x + s_col_mid[i + 1], LAYOUT.plot.y + s_face.hours[i + 1].temp_y));
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

static void prepare_frame(void) {
  int n = s_face.hour_count;
  int w = LAYOUT.plot.w;
  int i;
  for (i = 0; i < n; i++) {
    s_col_mid[i] = (int16_t)column_mid(i, n, w);
  }
  for (i = 0; i < s_face.day_count; i++) {
    layout_day_label(i, &s_day_labels[i]);
  }
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
    const DayLabel *label = &s_day_labels[i];
    int top_x1 = label->show_max ? label->max_x + label->max_size.w : label->x + label->name_size.w;
    if (top_x1 > label->x && r.origin.x < top_x1 + 1 && r.origin.x + r.size.w > label->x - 1 &&
        r.origin.y < LABEL_TOP_Y + 15 && r.origin.y + r.size.h > LABEL_TOP_Y + 3) {
      return true;
    }
    int by = label_bottom_y(label);
    if (label->show_min && r.origin.x < label->x + label->min_size.w + 1 &&
        r.origin.x + r.size.w > label->x - 1 && r.origin.y < by + 15 && r.origin.y + r.size.h > by + 3) {
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
    const DayLabel *label = &s_day_labels[i];
    if (label->name_size.w > 0) {
      draw_halo_text(ctx, t, label->name, font, label->x, LABEL_TOP_Y, label->name_size, t->muted);
    }
    if (label->show_max) {
      draw_halo_text(ctx, t, label->max, bold, label->max_x, LABEL_TOP_Y, label->max_size, t->text);
    }
    if (label->show_min) {
      draw_halo_text(ctx, t, label->min, bold, label->x, label_bottom_y(label), label->min_size,
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
  // A solid hairline in the muted tone: a dotted one breaks up on the reflective panel.
  fill_rect(ctx, x, y0, 1, LAYOUT.precip.y + LAYOUT.precip.h - y0, t->muted);
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
    int x = LAYOUT.plot.x + s_col_mid[i] - size.w / 2;
    draw_halo_text(ctx, t, text, font, x, LAYOUT.plot.y + LAYOUT.plot.h - size.h, size, t->muted);
  }
}

// Barbs go every few hours: the smallest step that divides a day and leaves room between them.
static int wind_step_hours(void) {
  static const int STEPS[] = {1, 2, 3, 4, 6, 8, 12, 24};
  int n = s_face.hour_count;
  int k;
  for (k = 0; k < (int)ARRAY_LENGTH(STEPS); k++) {
    if (STEPS[k] * LAYOUT.plot.w >= 17 * n) return STEPS[k];
  }
  return 24;
}

// ---- Wind barbs ----
//
// A barb's staff points to where the wind comes from, and the feathers at its far end give the
// speed in knots: a half feather is 5, a full one 10 and a filled pennant 50. Calm is a small
// ring. Directions snap to eight compass points so every stroke is horizontal, vertical or a
// true diagonal, which lands on whole pixels with no antialiasing blur.

#define BARB_MAX_PX 80

typedef struct {
  int8_t x;
  int8_t y;
} BarbPx;

typedef struct {
  BarbPx px[BARB_MAX_PX];
  int count;
} Barb;

// Unit steps for N, NE, E, SE, S, SW, W and NW, with y growing downward.
static const int8_t DIR8[8][2] = {
  {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}
};

static void barb_put(Barb *b, int x, int y) {
  int k;
  for (k = 0; k < b->count; k++) {
    if (b->px[k].x == x && b->px[k].y == y) return;
  }
  if (b->count < BARB_MAX_PX) {
    b->px[b->count].x = (int8_t)x;
    b->px[b->count].y = (int8_t)y;
    b->count += 1;
  }
}

static void barb_line(Barb *b, int x0, int y0, int x1, int y1) {
  int dx = x1 > x0 ? x1 - x0 : x0 - x1;
  int dy = y1 > y0 ? y0 - y1 : y1 - y0;
  int sx = x0 < x1 ? 1 : -1;
  int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    barb_put(b, x0, y0);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

// Builds a barb around the origin for compass octant 0..7 (N, NE, E, SE, S, SW, W, NW).
static void build_barb(Barb *b, int oct, int knots) {
  b->count = 0;
  int k5 = ((knots + 2) / 5) * 5;
  if (k5 < 5) {
    static const int8_t RING[12][2] = {
      {-1, -2}, {0, -2}, {1, -2}, {2, -1}, {2, 0}, {2, 1},
      {1, 2}, {0, 2}, {-1, 2}, {-2, 1}, {-2, 0}, {-2, -1}
    };
    int k;
    for (k = 0; k < 12; k++) barb_put(b, RING[k][0], RING[k][1]);
    return;
  }
  if (k5 > 95) k5 = 95;
  oct &= 7;
  const int8_t *sv = DIR8[oct];
  // Feathers sit on the left of the staff as seen looking downwind and slant toward its tip,
  // about 63 degrees off a straight staff and 72 off a diagonal one, near the 70 used on
  // weather maps. Both slopes are 2:1, which steps cleanly across whole pixels.
  const int8_t *pv = DIR8[(oct + 2) % 8];
  bool staff_diag = (oct & 1) != 0;
  int staff = staff_diag ? 6 : 8;
  int fx = staff_diag ? 3 * pv[0] + sv[0] : 4 * pv[0] + 2 * sv[0];
  int fy = staff_diag ? 3 * pv[1] + sv[1] : 4 * pv[1] + 2 * sv[1];
  int hx = fx / 2;
  int hy = fy / 2;
  int gap = 2;
  barb_line(b, 0, 0, sv[0] * staff, sv[1] * staff);
  int pos = staff;
  int n50 = k5 / 50;
  int n10 = (k5 % 50) / 10;
  int n5 = (k5 % 10) ? 1 : 0;
  // A lone half feather sits one step in from the tip, so it is not mistaken for a full one.
  if (n50 == 0 && n10 == 0) pos -= gap;
  int k;
  for (k = 0; k < n50 && pos >= gap; k++) {
    int tx = sv[0] * pos + fx;
    int ty = sv[1] * pos + fy;
    int j;
    for (j = 0; j <= gap; j++) barb_line(b, sv[0] * (pos - j), sv[1] * (pos - j), tx, ty);
    pos -= gap + 1;
  }
  for (k = 0; k < n10 && pos >= 1; k++) {
    barb_line(b, sv[0] * pos, sv[1] * pos, sv[0] * pos + fx, sv[1] * pos + fy);
    pos -= gap;
  }
  if (n5 && pos >= 1) {
    barb_line(b, sv[0] * pos, sv[1] * pos, sv[0] * pos + hx, sv[1] * pos + hy);
  }
}

// Draws a barb with its bounding box centered on (cx, cy).
static void draw_barb(GContext *ctx, int cx, int cy, int oct, int knots, GColor color) {
  Barb b;
  build_barb(&b, oct, knots);
  if (b.count == 0) return;
  int minx = b.px[0].x, maxx = b.px[0].x, miny = b.px[0].y, maxy = b.px[0].y;
  int k;
  for (k = 1; k < b.count; k++) {
    if (b.px[k].x < minx) minx = b.px[k].x;
    if (b.px[k].x > maxx) maxx = b.px[k].x;
    if (b.px[k].y < miny) miny = b.px[k].y;
    if (b.px[k].y > maxy) maxy = b.px[k].y;
  }
  int ox = cx - (minx + maxx) / 2;
  int oy = cy - (miny + maxy) / 2;
  graphics_context_set_stroke_color(ctx, color);
  for (k = 0; k < b.count; k++) graphics_draw_pixel(ctx, GPoint(ox + b.px[k].x, oy + b.px[k].y));
}

// Knots from the hour's speed, which is mph with Fahrenheit and km/h with Celsius.
static int hour_knots(const FaceHour *hour) {
  if (hour->wind_speed == FACE_NO_SPEED) return -1;
  if (s_face.metric) return (hour->wind_speed * 54 + 50) / 100;
  return (hour->wind_speed * 869 + 500) / 1000;
}

// The wind lane: speed is a soft area rising from the floor, and barbs every few hours give
// direction and speed.
static void draw_wind(GContext *ctx, const Theme *t) {
  int n = s_face.hour_count;
  if (n == 0) return;
  int w = LAYOUT.plot.w;
  int top = LAYOUT.wind.y;
  int floor_y = LAYOUT.wind.y + LAYOUT.wind.h;
  int px;
  int idx = 0;
  graphics_context_set_antialiased(ctx, false);
  graphics_context_set_stroke_width(ctx, 1);
  // A plain area with no edge line, so nothing crosses the barbs drawn over it.
  graphics_context_set_stroke_color(ctx, t->wind);
  for (px = 0; px < w; px++) {
    int h = field_at_step(px, &idx, &s_face.hours[0].wind_h, sizeof(FaceHour));
    if (h <= 0) continue;
    if (h > LAYOUT.wind.h) h = LAYOUT.wind.h;
    int x = LAYOUT.plot.x + px;
    graphics_draw_line(ctx, GPoint(x, floor_y - h), GPoint(x, floor_y - 1));
  }

  int step = wind_step_hours();
  int cy = top + LAYOUT.wind.h / 2;
  int i;
  for (i = 0; i < n; i++) {
    int secs = local_seconds(i);
    if (secs < 0 || (secs / 3600) % step != step / 2) continue;
    FaceHour hour = s_face.hours[i];
    int knots = hour_knots(&hour);
    if (hour.wind_dir == FACE_NO_DIR || knots < 0) continue;
    draw_barb(ctx, LAYOUT.plot.x + s_col_mid[i], cy, hour.wind_dir, knots, t->wind_barb);
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

static void draw_static_chart(GContext *ctx, const Theme *t) {
  draw_night(ctx, t);
  draw_sky(ctx, t);
  draw_fronts(ctx, t);
  draw_line(ctx, t);
  draw_wind(ctx, t);
  draw_precip(ctx, t);
  draw_day_labels(ctx, t);
  draw_hour_labels(ctx, t);
}

static void capture_chart_cache(GContext *ctx, const Theme *t) {
  if (!s_chart_cache) return;
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;
  GRect fb_bounds = gbitmap_get_bounds(fb);
  uint8_t *dst = gbitmap_get_data(s_chart_cache);
  uint16_t dst_stride = gbitmap_get_bytes_per_row(s_chart_cache);
  int row;
  for (row = 0; row < CHART_H; row++) {
    uint8_t *dst_row = dst + row * dst_stride;
    int sy = CHART_Y + row;
    if (sy < fb_bounds.origin.y || sy >= fb_bounds.origin.y + fb_bounds.size.h) {
      memset(dst_row, t->bg.argb, (size_t)CHART_W);
      continue;
    }
    GBitmapDataRowInfo info = gbitmap_get_data_row_info(fb, sy);
    int x0 = CHART_X;
    int x1 = CHART_X + CHART_W - 1;
    if (x0 < info.min_x) {
      memset(dst_row, t->bg.argb, (size_t)(info.min_x - x0));
      x0 = info.min_x;
    }
    if (x1 > info.max_x) {
      if (info.max_x + 1 < CHART_X + CHART_W) {
        memset(dst_row + (info.max_x + 1 - CHART_X), t->bg.argb,
               (size_t)(CHART_X + CHART_W - (info.max_x + 1)));
      }
      x1 = info.max_x;
    }
    if (x1 >= x0) {
      memcpy(dst_row + (x0 - CHART_X), info.data + x0, (size_t)(x1 - x0 + 1));
    }
  }
  graphics_release_frame_buffer(ctx, fb);
  s_chart_cache_valid = true;
}

static void draw_chart(GContext *ctx, const Theme *t, time_t now, bool cleared) {
  if (s_chart_cache && s_chart_cache_valid) {
    graphics_draw_bitmap_in_rect(ctx, s_chart_cache, GRect(CHART_X, CHART_Y, CHART_W, CHART_H));
  } else {
    if (!cleared) fill_rect(ctx, CHART_X, CHART_Y, CHART_W, CHART_H, t->bg);
    draw_static_chart(ctx, t);
    capture_chart_cache(ctx, t);
  }
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
#define TIME_TOP_PAD 14
#else
#define TIME_TOP_PAD 19
#endif

void face_init(void) {
#if defined(PBL_ROUND)
  s_time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TIME_48));
#else
  s_time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TIME_72));
#endif
  s_chart_cache = gbitmap_create_blank(GSize(CHART_W, CHART_H), GBitmapFormat8Bit);
  s_chart_cache_valid = false;
  s_last_24h = clock_is_24h_style();
}

void face_deinit(void) {
  if (s_chart_cache) gbitmap_destroy(s_chart_cache);
  s_chart_cache = NULL;
  s_chart_cache_valid = false;
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

// A clockwise refresh mark, arrowhead at the upper right. Whole pixels, like the battery and
// the Bluetooth rune, so it stays sharp. The ink is centered on (4, 4).
static const int8_t UPDATED_MARK[][2] = {
  {3, 1}, {4, 1}, {5, 1},
  {2, 2}, {6, 2}, {7, 2},
  {1, 3}, {7, 3}, {8, 3},
  {1, 4}, {8, 4},
  {1, 5},
  {2, 6}, {7, 6},
  {3, 7}, {4, 7}, {5, 7}, {6, 7}
};

static void draw_updated_mark(GContext *ctx, int cx, int cy, GColor color) {
  graphics_context_set_antialiased(ctx, false);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_stroke_color(ctx, color);
  int i;
  for (i = 0; i < (int)ARRAY_LENGTH(UPDATED_MARK); i++) {
    graphics_draw_pixel(ctx, GPoint(cx + UPDATED_MARK[i][0] - 4, cy + UPDATED_MARK[i][1] - 4));
  }
}

// Local time of the last successful fetch: the refresh mark, then the clock, following the
// watch's 12/24-hour setting. The stamp is the frame's fetched_at, which is stored with the
// forecast, so it is still there after a restart and does not move when a fetch fails.
// Before any success the time is "--:--", with the mark still beside it.
static void draw_updated(GContext *ctx, const Theme *t) {
  char text[8];
  const char *shown = "--:--";
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  if (s_face.ok && s_face.fetched_at > 0) {
    time_t fetched = (time_t)s_face.fetched_at;
    struct tm when = *localtime(&fetched);
    if (clock_is_24h_style()) {
      snprintf(text, sizeof(text), "%02d:%02d", when.tm_hour, when.tm_min);
    } else {
      int hour = when.tm_hour % 12;
      if (hour == 0) hour = 12;
      snprintf(text, sizeof(text), "%d:%02d", hour, when.tm_min);
    }
    shown = text;
  }
  GSize size = text_size(shown, font);
  // The mark's ink is 8px wide. A short gap, about a space, sits between it and the digits.
  int mark_w = 8;
  int gap = 3;
  int left = LAYOUT.windnow.x + (LAYOUT.windnow.w - mark_w - gap - size.w) / 2;
  // Digit ink is centered on the middle of this slot. The mark is centered on the same row.
  int cy = LAYOUT.windnow.y + LAYOUT.windnow.h / 2;
  draw_updated_mark(ctx, left + 3, cy, t->muted);
  draw_text(ctx, shown, font,
            GRect(left + mark_w + gap, LAYOUT.windnow.y - 1, size.w + 2, LAYOUT.windnow.h + 2),
            GTextAlignmentLeft, t->muted);
}

// The current hour's wind: a small barb and the speed, with the unit when it fits.
static void draw_wind_now(GContext *ctx, const Theme *t, time_t now) {
  int i = hour_now(now);
  if (i < 0) return;
  FaceHour hour = s_face.hours[i];
  if (hour.wind_speed == FACE_NO_SPEED) return;
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  char text[16];
  snprintf(text, sizeof(text), "%d %s", hour.wind_speed, s_face.metric ? "km/h" : "mph");
  bool arrow = hour.wind_dir != FACE_NO_DIR;
  int arrow_w = arrow ? 14 : 0;
  GSize size = text_size(text, font);
  if (arrow_w + size.w > LAYOUT.windnow.w - 6) {
    snprintf(text, sizeof(text), "%d", hour.wind_speed);
    size = text_size(text, font);
  }
  int x = LAYOUT.windnow.x + (LAYOUT.windnow.w - arrow_w - size.w) / 2;
  if (arrow) {
    draw_barb(ctx, x + 5, LAYOUT.windnow.y + LAYOUT.windnow.h / 2 + 1, hour.wind_dir,
              hour_knots(&hour), t->muted);
  }
  draw_text(ctx, text, font,
            GRect(x + arrow_w, LAYOUT.windnow.y - 1, size.w + 2, LAYOUT.windnow.h + 2),
            GTextAlignmentLeft, t->muted);
}

static int read_steps(time_t now) {
  if (!s_health_checked) {
    time_t start = time_start_of_today();
    time_t end = now;
    if (end <= start) end = start + 1;
    HealthServiceAccessibilityMask mask =
        health_service_metric_accessible(HealthMetricStepCount, start, end);
    s_health_avail = (mask & HealthServiceAccessibilityMaskAvailable) != 0;
    s_health_checked = true;
  }
  if (!s_health_avail) return -1;
  HealthValue steps = health_service_sum_today(HealthMetricStepCount);
  return steps >= 0 ? (int)steps : -1;
}

static void draw_steps(GContext *ctx, const Theme *t, time_t now) {
  if (s_last_steps == -2) s_last_steps = read_steps(now);
  if (s_last_steps < 0) return;
  char number[16];
  char text[24];
  format_grouped(number, s_last_steps);
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
  s_last_charge_pct = charge.charge_percent;
  s_last_charging = charge.is_charging;
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
  bool connected = bluetooth_connection_service_peek();
  s_last_bt = connected ? 1 : 0;
  if (connected) return;
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

bool face_needs_refresh(time_t now, uint32_t grace_sec) {
  if (!s_face.ok) return true;
  // Skip overnight Bluetooth wakeups during Quiet Time while the cached forecast covers now.
  if (quiet_time_is_active() && hour_now(now) >= 0) return false;
  int64_t interval =
      (int64_t)(s_face.refresh_min ? s_face.refresh_min : 60) * 60 + (int64_t)grace_sec;
  int64_t age = (int64_t)now - (int64_t)s_face.fetched_at;
  return age < -600 || age >= interval;
}

uint8_t face_tick_dirty(time_t now, TimeUnits units_changed) {
  uint8_t mask = FACE_DIRTY_TIME;
  bool h24 = clock_is_24h_style();
  if (h24 != s_last_24h) {
    s_chart_cache_valid = false;
    return FACE_DIRTY_ALL;
  }
  if (units_changed & DAY_UNIT) {
    s_health_checked = false;
    mask |= FACE_DIRTY_META;
  }
  if (s_face.ok) {
    int px = now_px(now);
    bool stale = false;
    int deg = temp_now(now, &stale);
    if (px != s_last_now_px || deg != s_last_temp_now || stale != s_last_temp_stale) {
      mask |= FACE_DIRTY_CHART;
      if ((px < 0) != (s_last_now_px < 0)) mask |= FACE_DIRTY_META;
    }
    if (!s_show_updated && hour_now(now) != s_last_hour_now) {
      mask |= FACE_DIRTY_TOP_CENTER;
    }
  }
  int steps = read_steps(now);
  if (steps != s_last_steps) {
    s_last_steps = steps;
    mask |= FACE_DIRTY_STEPS;
  }
  return mask;
}

bool face_battery_changed(BatteryChargeState charge) {
  return charge.charge_percent != s_last_charge_pct || charge.is_charging != s_last_charging;
}

bool face_bluetooth_changed(bool connected) {
  return s_last_bt != (connected ? 1 : 0);
}

void face_invalidate_all(void) {
  s_health_checked = false;
  s_last_steps = -2;
  if (clock_is_24h_style() != s_last_24h) s_chart_cache_valid = false;
}

void face_mark_dirty(uint8_t mask) {
  s_dirty |= mask;
}

void face_paint(Layer *layer, GContext *ctx) {
  Theme t = theme();
  GRect bounds = layer_get_bounds(layer);
  uint8_t dirty = s_dirty;
  s_dirty = 0;
  // If PebbleOS triggered a repaint directly (e.g. after a modal or screenshot), repaint all.
  bool full = (dirty == 0 || dirty == FACE_DIRTY_ALL);
  if (full) {
    dirty = FACE_DIRTY_ALL;
    fill_rect(ctx, bounds.origin.x, bounds.origin.y, bounds.size.w, bounds.size.h, t.bg);
  }
  time_t now = time(NULL);
  struct tm local = *localtime(&now);
  if (dirty & FACE_DIRTY_CHART) {
    if (s_face.ok) {
      draw_chart(ctx, &t, now, full);
    } else {
      if (!full) fill_rect(ctx, CHART_X, CHART_Y, CHART_W, CHART_H, t.bg);
      draw_waiting(ctx, &t);
    }
    s_last_now_px = now_px(now);
    s_last_temp_now = temp_now(now, &s_last_temp_stale);
  }
  if (dirty & FACE_DIRTY_TIME) {
    if (!full) fill_rect(ctx, TIME_CLEAR_X, TIME_CLEAR_Y, TIME_CLEAR_W, TIME_CLEAR_H, t.bg);
    draw_time(ctx, &t, &local);
  }
  if (dirty & FACE_DIRTY_META) {
    if (!full) fill_rect(ctx, LAYOUT.meta.x, LAYOUT.meta.y, LAYOUT.meta.w, LAYOUT.meta.h, t.bg);
    draw_meta(ctx, &t, &local, now);
  }
  if (dirty & FACE_DIRTY_STEPS) {
    if (!full) {
      fill_rect(ctx, LAYOUT.steps.x, LAYOUT.steps.y - 1, LAYOUT.steps.w, LAYOUT.steps.h + 2, t.bg);
    }
    draw_steps(ctx, &t, now);
  }
  if (dirty & FACE_DIRTY_BATTERY) {
    if (!full) {
      fill_rect(ctx, LAYOUT.battery.x, LAYOUT.battery.y - 1, LAYOUT.battery.w, LAYOUT.battery.h + 2,
                t.bg);
    }
    draw_battery(ctx, &t);
  }
  if (dirty & FACE_DIRTY_TOP_CENTER) {
    if (!full) {
      fill_rect(ctx, LAYOUT.windnow.x, LAYOUT.windnow.y - 1, LAYOUT.windnow.w, LAYOUT.windnow.h + 2,
                t.bg);
    }
    if (s_show_updated) draw_updated(ctx, &t);
    else draw_wind_now(ctx, &t, now);
    s_last_hour_now = hour_now(now);
  }
  if (dirty & FACE_DIRTY_BT) {
    int h = LAYOUT.bt_r + 2;
    if (!full) fill_rect(ctx, LAYOUT.bt_x - h, LAYOUT.bt_y - h, 2 * h + 1, 2 * h + 1, t.bg);
    draw_bluetooth(ctx);
  }
  s_last_24h = clock_is_24h_style();
}
