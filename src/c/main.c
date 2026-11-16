#include "face.h"

static Window *s_window;
static Layer *s_layer;

static void redraw(void) {
  if (s_layer) layer_mark_dirty(s_layer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  (void)tick_time;
  (void)units_changed;
  redraw();
}

static void battery_handler(BatteryChargeState charge) {
  (void)charge;
  redraw();
}

static void bluetooth_handler(bool connected) {
  (void)connected;
  redraw();
}

static void health_handler(HealthEventType event, void *context) {
  (void)event;
  (void)context;
  redraw();
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  (void)context;
  Tuple *tuple = dict_find(iter, MESSAGE_KEY_PAYLOAD);
  if (!tuple || tuple->type != TUPLE_BYTE_ARRAY) return;
  if (!face_set_frame(tuple->value->data, tuple->length)) return;
  redraw();
}

static void init(void) {
  app_message_register_inbox_received(inbox_received);
  uint32_t inbox = app_message_inbox_size_maximum();
  app_message_open(inbox, 64);
  APP_LOG(APP_LOG_LEVEL_INFO, "inbox %lu", (unsigned long)inbox);

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
  health_service_events_subscribe(health_handler, NULL);
}

static void deinit(void) {
  health_service_events_unsubscribe();
  bluetooth_connection_service_unsubscribe();
  battery_state_service_unsubscribe();
  tick_timer_service_unsubscribe();
  layer_destroy(s_layer);
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
