#include "face.h"

static Window *s_window;
static Layer *s_layer;

static void redraw(void) {
  if (s_layer) layer_mark_dirty(s_layer);
}

// The watch asks the phone for a refresh on its own schedule, because the phone
// may stop the Pebble app's script in the background and never wake its own timer.
// Once an hour is enough for that backstop; the phone runs the user's interval itself.
static void request_refresh(void) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) return;
#if defined(PBL_ROUND)
  dict_write_uint8(out, MESSAGE_KEY_REQUEST, 2);
#else
  dict_write_uint8(out, MESSAGE_KEY_REQUEST, 1);
#endif
  app_message_outbox_send();
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  (void)units_changed;
  if (tick_time->tm_min == 0) request_refresh();
  redraw();
}

static void battery_handler(BatteryChargeState charge) {
  (void)charge;
  redraw();
}

static void bluetooth_handler(bool connected) {
  if (connected) request_refresh();
  redraw();
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
  bool dirty = false;
  // The choice can arrive on its own, before any forecast, so the top line updates immediately.
  int top = tuple_number(dict_find(iter, MESSAGE_KEY_TOPLINE));
  if (top == 0 || top == 1) {
    face_set_top_line(top == 1);
    dirty = true;
  }
  Tuple *tuple = dict_find(iter, MESSAGE_KEY_PAYLOAD);
  if (tuple && tuple->type == TUPLE_BYTE_ARRAY &&
      face_set_frame(tuple->value->data, tuple->length)) {
    dirty = true;
  }
  if (dirty) redraw();
}

static void init(void) {
  app_message_register_inbox_received(inbox_received);
  uint32_t inbox = app_message_inbox_size_maximum();
  app_message_open(inbox, 64);

  face_init();
  face_load();

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  Layer *root = window_get_root_layer(s_window);
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, face_paint);
  layer_add_child(root, s_layer);
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);
  bluetooth_connection_service_subscribe(bluetooth_handler);
}

static void deinit(void) {
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
