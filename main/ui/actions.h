#ifndef EEZ_LVGL_UI_EVENTS_H
#define EEZ_LVGL_UI_EVENTS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void action_click_save(lv_event_t * e);
extern void action_click_new_file(lv_event_t * e);
extern void action_click_start(lv_event_t * e);
extern void action_click_cancel(lv_event_t * e);
extern void action_click_filename(lv_event_t * e);
extern void action_pressed_fuel_consumption(lv_event_t * e);
extern void action_released_fuel_consumption(lv_event_t * e);
extern void action_switch_changed(lv_event_t * e);
extern void action_click_rtc(lv_event_t * e);
extern void action_click_ssid(lv_event_t * e);
extern void action_click_password(lv_event_t * e);
extern void action_click_disconnect(lv_event_t * e);
extern void action_click_connect(lv_event_t * e);
extern void action_cancel_keyboard_wifi(lv_event_t * e);
extern void action_ready_keyboard_wifi(lv_event_t * e);
extern void action_click_reset_orchard(lv_event_t * e);
extern void action_click_reset_eco(lv_event_t * e);
extern void action_brightness_changed(lv_event_t * e);
extern void action_brightness_released(lv_event_t * e);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_EVENTS_H*/