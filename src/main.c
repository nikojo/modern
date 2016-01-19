#include <pebble.h>
#include "common.h"
#include "effect_layer.h"

#define FOREGROUND GColorWhite
#define BACKGROUND GColorBlack

#define KEY_MINUTE_COLOR_R 0
#define KEY_MINUTE_COLOR_G 1
#define KEY_MINUTE_COLOR_B 2
#define KEY_HOUR_COLOR_R 3
#define KEY_HOUR_COLOR_G 4
#define KEY_HOUR_COLOR_B 5


static Window* window;
static GBitmap *background_image_container;

static Layer *minute_display_layer;
static Layer *hour_display_layer;
static Layer *center_display_layer;
static TextLayer *date_layer;
static TextLayer *battpercent_layer;
static char date_text[] = "Wed 13 ";
static char battpercent_text[] = "100% ";
static bool bluetooth_ok = false;
static uint8_t battery_level;
static bool battery_plugged;

static GBitmap *icon_battery;
static GBitmap *icon_battery_charge;
static GBitmap *icon_bt;

static Layer *battery_layer;

bool g_conserve = false;

static EffectLayer *full_inverse_layer;

static Layer *background_layer;
static Layer *window_layer;
static Layer *inverter_layer;

const GPathInfo MINUTE_HAND_PATH_POINTS = { 4, (GPoint[] ) { { -4, 15 },
				{ 4, 15 }, { 4, -70 }, { -4, -70 }, } };

const GPathInfo HOUR_HAND_PATH_POINTS = { 4, (GPoint[] ) { { -4, 15 },
				{ 4, 15 }, { 4, -50 }, { -4, -50 }, } };

static GPath *hour_hand_path;
static GPath *minute_hand_path;

static GColor minute_color;
static GColor hour_color;

static void inbox_received_callback(DictionaryIterator *iter, void *context) {
  int red, green, blue;
  Tuple *color_red_t, *color_green_t, *color_blue_t;
  // Minute color?
  color_red_t = dict_find(iter, KEY_MINUTE_COLOR_R);
  color_green_t = dict_find(iter, KEY_MINUTE_COLOR_G);
  color_blue_t = dict_find(iter, KEY_MINUTE_COLOR_B);
  if(color_red_t && color_green_t && color_blue_t) {
    // Apply the color if available
      red = color_red_t->value->int32;
      green = color_green_t->value->int32;
      blue = color_blue_t->value->int32;
  
      // Persist values
      persist_write_int(KEY_MINUTE_COLOR_R, red);
      persist_write_int(KEY_MINUTE_COLOR_G, green);
      persist_write_int(KEY_MINUTE_COLOR_B, blue);
  
      minute_color = GColorFromRGB(red, green, blue);
  }
  // Hour color?
  color_red_t = dict_find(iter, KEY_HOUR_COLOR_R);
  color_green_t = dict_find(iter, KEY_HOUR_COLOR_G);
  color_blue_t = dict_find(iter, KEY_HOUR_COLOR_B);
  if(color_red_t && color_green_t && color_blue_t) {
    // Apply the color if available
      red = color_red_t->value->int32;
      green = color_green_t->value->int32;
      blue = color_blue_t->value->int32;
  
      // Persist values
      persist_write_int(KEY_HOUR_COLOR_R, red);
      persist_write_int(KEY_HOUR_COLOR_G, green);
      persist_write_int(KEY_HOUR_COLOR_B, blue);
  
      hour_color = GColorFromRGB(red, green, blue);
  }

  layer_mark_dirty(minute_display_layer);
  layer_mark_dirty(hour_display_layer);

}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped!");
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed!");
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send success!");
}

void center_display_layer_update_callback(Layer *me, GContext* ctx) {
	(void) me;

	GPoint center = grect_center_point(&GRECT_FULL_WINDOW);
	graphics_context_set_fill_color(ctx, BACKGROUND);
	graphics_fill_circle(ctx, center, 4);
	graphics_context_set_fill_color(ctx, FOREGROUND);
	graphics_fill_circle(ctx, center, 3);
}

void minute_display_layer_update_callback(Layer *me, GContext* ctx) {
	(void) me;

	time_t now = time(NULL);
	struct tm *t = localtime(&now);

	unsigned int angle = t->tm_min * 6; // + t->tm_sec / 10;

	gpath_rotate_to(minute_hand_path, (TRIG_MAX_ANGLE / 360) * angle);

	graphics_context_set_fill_color(ctx, minute_color);
	graphics_context_set_stroke_color(ctx, minute_color);

	gpath_draw_filled(ctx, minute_hand_path);
	gpath_draw_outline(ctx, minute_hand_path);
}

void hour_display_layer_update_callback(Layer *me, GContext* ctx) {
	(void) me;

	time_t now = time(NULL);
	struct tm *t = localtime(&now);

	unsigned int angle = t->tm_hour * 30 + t->tm_min / 2;

	gpath_rotate_to(hour_hand_path, (TRIG_MAX_ANGLE / 360) * angle);

	graphics_context_set_fill_color(ctx, hour_color);
	graphics_context_set_stroke_color(ctx, hour_color);

	gpath_draw_filled(ctx, hour_hand_path);
	gpath_draw_outline(ctx, hour_hand_path);
}

void draw_date() {
	time_t now = time(NULL);
	struct tm *t = localtime(&now);

	strftime(date_text, sizeof(date_text), "%a %e", t);

	text_layer_set_text(date_layer, date_text);
}

/*
 * Battery icon callback handler
 */
void battery_layer_update_callback(Layer *layer, GContext *ctx) {

  graphics_context_set_compositing_mode(ctx, GCompOpSet);

  snprintf(battpercent_text, sizeof(battpercent_text), "%d", battery_level);
  text_layer_set_text(battpercent_layer, battpercent_text);
  if (!battery_plugged) {
    graphics_draw_bitmap_in_rect(ctx, icon_battery, GRect(0, 0, 24, 12));
    graphics_context_set_fill_color(ctx, FOREGROUND);
    graphics_fill_rect(ctx, GRect(6, 3, (uint8_t)((battery_level / 100.0) * 13.0), 6), 0, GCornerNone);
  } else {
    graphics_draw_bitmap_in_rect(ctx, icon_battery_charge, GRect(0, 0, 24, 12));
  }
}



void battery_state_handler(BatteryChargeState charge) {
	battery_level = charge.charge_percent;
	battery_plugged = charge.is_plugged;
	layer_mark_dirty(battery_layer);
}

static AppTimer *recheck_bluetooth_timer;

void recheck_bluetooth(void *data) {
    app_timer_cancel(recheck_bluetooth_timer);
    if (!bluetooth_connection_service_peek()) {
        // still disconnected, so vibrate
        vibes_long_pulse();
	layer_add_child(window_layer, inverter_layer);
    }
}

void bluetooth_connection_handler(bool connected) {
	bluetooth_ok = connected;
        if (connected) {
            layer_remove_from_parent(inverter_layer);
        } else {
            recheck_bluetooth_timer = app_timer_register(3000, recheck_bluetooth, NULL);
        }
}

void draw_background_callback(Layer *layer, GContext *ctx) {
	graphics_context_set_compositing_mode(ctx, GCompOpAssign);
	graphics_draw_bitmap_in_rect(ctx, background_image_container,
			GRECT_FULL_WINDOW);
}

void handle_tick(struct tm *tick_time, TimeUnits units_changed) {

	layer_mark_dirty(minute_display_layer);

	if (tick_time->tm_min % 2 == 0) {
		layer_mark_dirty(hour_display_layer);
		if (tick_time->tm_min == 0 && tick_time->tm_hour == 0) {
			draw_date();
		}
	}
}

void init() {

        // Register callbacks
        app_message_register_inbox_received(inbox_received_callback);
        app_message_register_inbox_dropped(inbox_dropped_callback);
        app_message_register_outbox_failed(outbox_failed_callback);
        app_message_register_outbox_sent(outbox_sent_callback);

        // Open AppMessage
        app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());  
  
        // Get stored colors
        #ifdef PBL_COLOR
        int red, green, blue;
        if (persist_exists(KEY_MINUTE_COLOR_R)) {
            red = persist_read_int(KEY_MINUTE_COLOR_R);
            green = persist_read_int(KEY_MINUTE_COLOR_G);
	    blue = persist_read_int(KEY_MINUTE_COLOR_B);
	    minute_color = GColorFromRGB(red, green, blue);
        } else {
	    minute_color = GColorLimerick;
        }
	if (persist_exists(KEY_HOUR_COLOR_R)) {
	    red = persist_read_int(KEY_HOUR_COLOR_R);
	    green = persist_read_int(KEY_HOUR_COLOR_G);
	    blue = persist_read_int(KEY_HOUR_COLOR_B);  
	    hour_color = GColorFromRGB(red, green, blue);
	} else {
	    hour_color = GColorBabyBlueEyes;
	}
	#endif
    
	// Window
	window = window_create();
	window_stack_push(window, true /* Animated */);
	window_layer = window_get_root_layer(window);

	// Background image
	background_image_container = gbitmap_create_with_resource(
			RESOURCE_ID_IMAGE_BACKGROUND);
	background_layer = layer_create(GRECT_FULL_WINDOW);
	layer_set_update_proc(background_layer, &draw_background_callback);
	layer_add_child(window_layer, background_layer);

	// Hands setup
	hour_display_layer = layer_create(GRECT_FULL_WINDOW);
	layer_set_update_proc(hour_display_layer,
			&hour_display_layer_update_callback);
	layer_add_child(window_layer, hour_display_layer);

	hour_hand_path = gpath_create(&HOUR_HAND_PATH_POINTS);
	gpath_move_to(hour_hand_path, grect_center_point(&GRECT_FULL_WINDOW));

	minute_display_layer = layer_create(GRECT_FULL_WINDOW);
	layer_set_update_proc(minute_display_layer,
			&minute_display_layer_update_callback);
	layer_add_child(window_layer, minute_display_layer);

	minute_hand_path = gpath_create(&MINUTE_HAND_PATH_POINTS);
	gpath_move_to(minute_hand_path, grect_center_point(&GRECT_FULL_WINDOW));

	center_display_layer = layer_create(GRECT_FULL_WINDOW);
	layer_set_update_proc(center_display_layer,
			&center_display_layer_update_callback);
	layer_add_child(window_layer, center_display_layer);

	// Date setup
	//date_layer = text_layer_create(GRect(27, 100, 90, 21));
	date_layer = text_layer_create(GRect(20, 100, 106, 33));
	text_layer_set_text_color(date_layer, FOREGROUND);
	text_layer_set_text_alignment(date_layer, GTextAlignmentCenter);
	text_layer_set_background_color(date_layer, GColorClear);
	text_layer_set_font(date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
	layer_add_child(window_layer, text_layer_get_layer(date_layer));

	draw_date();

	// Status setup
	icon_battery = gbitmap_create_with_resource(RESOURCE_ID_BATTERY_ICON);
	icon_battery_charge = gbitmap_create_with_resource(RESOURCE_ID_BATTERY_CHARGE);

	BatteryChargeState initial = battery_state_service_peek();
	battery_level = initial.charge_percent;
	battery_plugged = initial.is_plugged;
	//battery_layer = layer_create(GRect(37, 41, 24, 12));
	battery_layer = layer_create(GRect(47, 134, 24, 12));
	layer_set_update_proc(battery_layer, &battery_layer_update_callback);
	layer_add_child(window_layer, battery_layer);

	//battpercent_layer = text_layer_create(GRect(53, 30, 50, 24));
	battpercent_layer = text_layer_create(GRect(65, 123, 30, 24));
	text_layer_set_text_color(battpercent_layer, FOREGROUND);
	text_layer_set_text_alignment(battpercent_layer, GTextAlignmentRight);
	text_layer_set_background_color(battpercent_layer, GColorClear);
	text_layer_set_font(battpercent_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
	layer_add_child(window_layer, text_layer_get_layer(battpercent_layer));
	

	full_inverse_layer = effect_layer_create(GRECT_FULL_WINDOW);
	effect_layer_add_effect(full_inverse_layer, effect_invert, NULL);
        inverter_layer = effect_layer_get_layer(full_inverse_layer);

	bluetooth_connection_handler(bluetooth_connection_service_peek());

	tick_timer_service_subscribe(MINUTE_UNIT, &handle_tick);
}

void deinit() {

	window_destroy(window);
	gbitmap_destroy(background_image_container);
	gbitmap_destroy(icon_battery);
	gbitmap_destroy(icon_battery_charge);
	gbitmap_destroy(icon_bt);
	text_layer_destroy(date_layer);
	text_layer_destroy(battpercent_layer);
	layer_destroy(minute_display_layer);
	layer_destroy(hour_display_layer);
	layer_destroy(center_display_layer);
	layer_destroy(battery_layer);

	effect_layer_destroy(full_inverse_layer);

	layer_destroy(background_layer);

	gpath_destroy(hour_hand_path);
	gpath_destroy(minute_hand_path);

}

/*
 * Main - or main as it is known
 */
int main(void) {
	init();
	bluetooth_connection_service_subscribe(&bluetooth_connection_handler);
	battery_state_service_subscribe	(&battery_state_handler);
	app_event_loop();
	deinit();
}

