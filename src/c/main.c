#include "face.h"

#if defined(PBL_ROUND)
#define REQUEST_PLATFORM 2
#else
#define REQUEST_PLATFORM 1
#endif
#define REQUEST_NEED_FRAME 0x80

static Window *s_window;
static Layer *s_layer;

static void paint_regions(uint8_t mask) {
  if (!s_layer || !mask) return;
  face_mark_dirty(mask);
  layer_mark_dirty(s_layer);
}

// The watch asks the phone for a refresh only when the stored frame is missing or past the
// configured refresh interval, so when the phone's own timer is running the watch stays silent
// over Bluetooth. Setting `need_frame` tells the phone to resend even if its hash matches.
static void request_refresh(bool need_frame) {
  if (!bluetooth_connection_service_peek()) return;
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) return;
  uint8_t code = REQUEST_PLATFORM | (need_frame ? REQUEST_NEED_FRAME : 0);
  dict_write_uint8(out, MESSAGE_KEY_REQUEST, code);
  app_message_outbox_send();
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  time_t now = time(NULL);
  if ((units_changed & DAY_UNIT) && !quiet_time_is_active()) {
    request_refresh(!face_has_frame());
  } else if (tick_time->tm_min % 15 == 0 && face_needs_refresh(now, 120)) {
    request_refresh(!face_has_frame());
  }
  uint8_t dirty = face_tick_dirty(now, units_changed);
  paint_regions(dirty);
}

static void battery_handler(BatteryChargeState charge) {
  if (face_battery_changed(charge)) paint_regions(FACE_DIRTY_BATTERY);
}

static void bluetooth_handler(bool connected) {
  if (connected && face_needs_refresh(time(NULL), 0)) {
    request_refresh(!face_has_frame());
  }
  if (face_bluetooth_changed(connected)) paint_regions(FACE_DIRTY_BT);
}

static void window_appear(Window *window) {
  (void)window;
  face_invalidate_all();
  paint_regions(FACE_DIRTY_ALL);
}

static void focus_did_change(bool in_focus) {
  if (in_focus) {
    face_invalidate_all();
    paint_regions(FACE_DIRTY_ALL);
  }
}

static void unobstructed_did_change(void *context) {
  (void)context;
  face_invalidate_all();
  paint_regions(FACE_DIRTY_ALL);
}

static int tuple_number(const Tuple *tuple) {
  if (!tuple) return -1;
  if (tuple->type != TUPLE_UINT && tuple->type != TUPLE_INT) return -1;
  if (tuple->length == 1) return tuple->value->uint8;
  if (tuple->length == 2) return tuple->value->uint16;
  return tuple->value->int32;
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  (void)context;
  uint8_t dirty = 0;
  // The choice can arrive on its own, before any forecast, so the top line updates immediately.
  int top = tuple_number(dict_find(iter, MESSAGE_KEY_TOPLINE));
  if ((top == 0 || top == 1) && face_set_top_line(top == 1)) {
    dirty |= FACE_DIRTY_TOP_CENTER;
  }
  Tuple *tuple = dict_find(iter, MESSAGE_KEY_PAYLOAD);
  if (tuple && tuple->type == TUPLE_BYTE_ARRAY) {
    dirty |= face_set_frame(tuple->value->data, tuple->length);
  }
  paint_regions(dirty);
}

static void init(void) {
  app_message_register_inbox_received(inbox_received);
  uint32_t max_inbox = app_message_inbox_size_maximum();
  uint32_t inbox = FACE_MAX_BYTES + 64;
  if (inbox > max_inbox) inbox = max_inbox;
  app_message_open(inbox, 64);

  face_init();
  face_load();

  s_window = window_create();
  // Keep the framebuffer between partial repaints so normal minute ticks only flush the time band.
  window_set_background_color(s_window, GColorClear);
  window_set_window_handlers(s_window, (WindowHandlers){
    .appear = window_appear,
  });
  Layer *root = window_get_root_layer(s_window);
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, face_paint);
  layer_add_child(root, s_layer);
  window_stack_push(s_window, false);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);
  bluetooth_connection_service_subscribe(bluetooth_handler);
  app_focus_service_subscribe_handlers((AppFocusHandlers){
    .did_focus = focus_did_change,
  });
  unobstructed_area_service_subscribe((UnobstructedAreaHandlers){
    .did_change = unobstructed_did_change,
  }, NULL);

  if (!face_has_frame()) request_refresh(true);
}

static void deinit(void) {
  unobstructed_area_service_unsubscribe();
  app_focus_service_unsubscribe();
  bluetooth_connection_service_unsubscribe();
  battery_state_service_unsubscribe();
  tick_timer_service_unsubscribe();
  layer_destroy(s_layer);
  window_destroy(s_window);
  face_deinit();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
