#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_SCREEN_MAIN = 1,
    _SCREEN_ID_LAST = 1
};

typedef struct _objects_t {
    lv_obj_t *screen_main;
    lv_obj_t *tabview_main;
    lv_obj_t *tab_ai;
    lv_obj_t *container_can_1;
    lv_obj_t *meter_score;
    lv_obj_t *tab_can_data;
    lv_obj_t *container_can;
    lv_obj_t *panel_brake_pedal;
    lv_obj_t *label_b_pedal;
    lv_obj_t *panel_speed;
    lv_obj_t *label_speed;
    lv_obj_t *panel_ignition;
    lv_obj_t *label_ignition;
    lv_obj_t *panel_acc_pedal;
    lv_obj_t *label_acc;
    lv_obj_t *panel_rpm;
    lv_obj_t *label_rpm;
    lv_obj_t *panel_range;
    lv_obj_t *label_range;
    lv_obj_t *panel__fuel_level;
    lv_obj_t *label_f_level;
    lv_obj_t *panel_fuel_consumption;
    lv_obj_t *label_fuel_consumption;
    lv_obj_t *panel_oil_temp;
    lv_obj_t *label_oil_temp;
    lv_obj_t *panel_total_distance;
    lv_obj_t *label_t_distance;
    lv_obj_t *panel_bits;
    lv_obj_t *container_rear;
    lv_obj_t *label_rear;
    lv_obj_t *container_esp;
    lv_obj_t *label_esp;
    lv_obj_t *tab_save;
    lv_obj_t *container_save;
    lv_obj_t *container_sd_wifi;
    lv_obj_t *container_sd;
    lv_obj_t *label_status;
    lv_obj_t *label_file;
    lv_obj_t *button_save;
    lv_obj_t *button_new__file;
    lv_obj_t *container_wifi;
    lv_obj_t *label_wifi_status;
    lv_obj_t *textarea_wifi_ssid;
    lv_obj_t *textarea_wifi_password;
    lv_obj_t *button_wifi_disconnect;
    lv_obj_t *button_wifi_connect;
    lv_obj_t *container_settings;
    lv_obj_t *container_bl;
    lv_obj_t *switch_bl;
    lv_obj_t *label_switch;
    lv_obj_t *button_reset_orchard;
    lv_obj_t *button_reset_eco;
    lv_obj_t *label_ai_model;
    lv_obj_t *slider_brightness;
    lv_obj_t *container_rtc;
    lv_obj_t *label_time_status;
    lv_obj_t *label_time;
    lv_obj_t *button_rtc;
    lv_obj_t *keyboard_wi_fi;
    lv_obj_t *panel_new_file;
    lv_obj_t *textarea_filename;
    lv_obj_t *keyboard_new_file;
    lv_obj_t *button_start;
    lv_obj_t *button_cancel;
} objects_t;

extern objects_t objects;

typedef struct {
    lv_meter_scale_t *scale;
    lv_meter_indicator_t *indicator;
} screen_screen_main_state_t;

extern screen_screen_main_state_t screen_screen_main_state;

void create_screen_screen_main();
void tick_screen_screen_main();

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/