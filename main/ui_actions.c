#include "ui/actions.h"
#include "ui/screens.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "can.h"
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "sd_card.h"
#include "wifi_manager.h"
#include "rtc_manager.h"
#include "ai_model.h"
#include "eco_stats.h"
#include "nvs.h"

static const char *TAG = "ui_actions";

static void focus_filename_input(void);
static void focus_wifi_input(lv_obj_t *textarea);
static void hide_wifi_keyboard(void);
static void set_backlight_state(bool on);
static void update_backlight_mode_label(void);
static void register_backlight_short_click_handlers(void);
static void action_screen_short_clicked(lv_event_t * e);
static void update_wifi_status_label(void);
static void update_time_label(void);
static void update_time_sync_label(void);
static void update_sd_mode_controls(void);
esp_err_t wavesahre_rgb_lcd_bl_on(void);
esp_err_t wavesahre_rgb_lcd_bl_off(void);

static TickType_t press_start_time = 0;

static bool s_bl_user_control = false;
static int8_t s_backlight_state = -1; // -1 unknown, 0 off, 1 on
static uint32_t s_last_short_click_ms = 0;
static bool s_short_click_handlers_registered = false;
static bool s_save_controls_initialized = false;

#define UI_PAGER_MAX_DOTS 8
static lv_obj_t *s_page_dots[UI_PAGER_MAX_DOTS];
static uint32_t s_page_dot_count = 0;

// Screen "brightness": the backlight pin sits on the CH422G I2C expander
// (on/off only, no PWM), so dimming is simulated with a black overlay on the
// LVGL top layer whose opacity grows as brightness drops. The overlay is not
// clickable, so touches pass through. Range 20-100% keeps the screen readable.
#define UI_BRIGHTNESS_MIN     20
#define UI_BRIGHTNESS_MAX     100
#define UI_BRIGHTNESS_NVS_NS  "storage"
#define UI_BRIGHTNESS_NVS_KEY "brightness"

static lv_obj_t *s_dim_overlay = NULL;

static void apply_brightness(int32_t percent) {
    if (percent < UI_BRIGHTNESS_MIN) percent = UI_BRIGHTNESS_MIN;
    if (percent > UI_BRIGHTNESS_MAX) percent = UI_BRIGHTNESS_MAX;

    if (s_dim_overlay == NULL) {
        s_dim_overlay = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(s_dim_overlay);
        lv_obj_clear_flag(s_dim_overlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(s_dim_overlay, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(s_dim_overlay, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    lv_opa_t opa = (lv_opa_t)((UI_BRIGHTNESS_MAX - percent) * 255 / UI_BRIGHTNESS_MAX);
    if (opa == 0) {
        lv_obj_add_flag(s_dim_overlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_style_bg_opa(s_dim_overlay, opa, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(s_dim_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

// Keep the LVGL FPS/CPU monitor off the AI page (clean scene) but visible on
// the other pages. The monitor is a plain label LVGL creates lazily on the
// system layer at the first refresh, so it is looked up on demand; until it
// exists this is a no-op and ui_save_tick() retries periodically.
static void update_perf_monitor_visibility(void) {
    if (objects.tabview_main == NULL) {
        return;
    }

    lv_obj_t *label = NULL;
    lv_obj_t *sys = lv_layer_sys();
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(sys); i++) {
        lv_obj_t *child = lv_obj_get_child(sys, i);
        if (lv_obj_check_type(child, &lv_label_class)) {
            label = child;
            break;
        }
    }
    if (label == NULL) {
        return;
    }

    uint32_t act = lv_tabview_get_tab_act(objects.tabview_main);
    lv_obj_t *page = lv_obj_get_child(lv_tabview_get_content(objects.tabview_main), act);
    if (page == objects.tab_ai || page == eco_stats_get_page()) {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_page_dots(uint32_t active_idx) {
    for (uint32_t i = 0; i < s_page_dot_count; i++) {
        bool active = (i == active_idx);
        lv_obj_set_style_bg_color(s_page_dots[i],
            lv_color_hex(active ? 0xff333333 : 0xffb0b0b0), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_size(s_page_dots[i], active ? 14 : 10, active ? 14 : 10);
    }
    update_perf_monitor_visibility();
}

// Center text of all labels inside a tile (handles nested containers).
static void center_labels_recursive(lv_obj_t *obj) {
    if (lv_obj_check_type(obj, &lv_label_class)) {
        lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        return;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); i++) {
        center_labels_recursive(lv_obj_get_child(obj, i));
    }
}

static void tabview_page_changed_cb(lv_event_t *e) {
    lv_obj_t *tv = lv_event_get_target(e);
    update_page_dots(lv_tabview_get_tab_act(tv));
}

// Swipe anywhere on the screen switches pages, wrapping around at both ends
// (last -> first and first -> last). Native content scrolling is disabled in
// ui_pager_init(), so gestures are the only page navigation.
static void screen_gesture_cb(lv_event_t *e) {
    LV_UNUSED(e);

    lv_obj_t *tv = objects.tabview_main;
    if (tv == NULL) {
        return;
    }

    // Don't flip pages while dragging horizontal controls (switch/slider).
    lv_indev_t *indev = lv_indev_get_act();
    lv_obj_t *pressed = lv_indev_get_obj_act();
    if (indev == NULL) {
        return;
    }
    if (pressed != NULL && (lv_obj_check_type(pressed, &lv_switch_class) ||
                            lv_obj_check_type(pressed, &lv_slider_class))) {
        return;
    }

    uint32_t count = lv_obj_get_child_cnt(lv_tabview_get_content(tv));
    if (count < 2) {
        return;
    }

    uint32_t act = lv_tabview_get_tab_act(tv);
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    uint32_t next;
    if (dir == LV_DIR_LEFT) {
        next = (act + 1) % count;
    } else if (dir == LV_DIR_RIGHT) {
        next = (act + count - 1) % count;
    } else {
        return;
    }

    lv_tabview_set_act(tv, next, LV_ANIM_ON);
    // lv_tabview_set_act() never emits VALUE_CHANGED (the tabview only sends
    // it for finger-scrolled page changes), so refresh the dots here.
    update_page_dots(next);
}

void ui_pager_init(void) {
    lv_obj_t *tv = objects.tabview_main;
    if (tv == NULL) {
        return;
    }

    // Hide tab button bar; tabview flex layout expands content to full screen.
    lv_obj_add_flag(lv_tabview_get_tab_btns(tv), LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *content = lv_tabview_get_content(tv);

    // Loop-around paging: turn off the tabview's own swipe-scroll (it stops
    // dead at the first/last page) and drive page changes from full-screen
    // gestures instead — see screen_gesture_cb(). Programmatic
    // lv_tabview_set_act() still animates via lv_obj_scroll_to_x().
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(objects.screen_main, screen_gesture_cb, LV_EVENT_GESTURE, NULL);
    uint32_t tab_count = lv_obj_get_child_cnt(content);
    if (tab_count > UI_PAGER_MAX_DOTS) {
        tab_count = UI_PAGER_MAX_DOTS;
    }
    s_page_dot_count = tab_count;

    // The legacy score meter (generated by EEZ Studio) is replaced by the
    // eco-tree view; delete it before the first render so it never shows up.
    if (objects.meter_score != NULL) {
        lv_obj_del(objects.meter_score);
        objects.meter_score = NULL;
    }

    // Full-bleed pages: strip the theme padding from every tab page and the
    // corner radius from each page's root container. The old "framed card"
    // look is gone — page backgrounds (grey on CAN/SAVE, meadow on AI) run
    // edge to edge.
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(content); i++) {
        lv_obj_t *page = lv_obj_get_child(content, i);
        lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        for (uint32_t j = 0; j < lv_obj_get_child_cnt(page); j++) {
            lv_obj_set_style_radius(lv_obj_get_child(page, j), 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    // Scale CAN tiles up to fill their grid cells; fixed gaps between tiles.
    // pad_bottom 30 keeps the last grid row above the dot indicator overlay.
    if (objects.container_can != NULL) {
        lv_obj_set_style_pad_left(objects.container_can, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_right(objects.container_can, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_bottom(objects.container_can, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_row(objects.container_can, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_column(objects.container_can, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
        for (uint32_t j = 0; j < lv_obj_get_child_cnt(objects.container_can); j++) {
            lv_obj_t *tile = lv_obj_get_child(objects.container_can, j);
            lv_obj_set_style_grid_cell_x_align(tile, LV_GRID_ALIGN_STRETCH, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_grid_cell_y_align(tile, LV_GRID_ALIGN_STRETCH, LV_PART_MAIN | LV_STATE_DEFAULT);
            // Spread title/value labels over the taller tile.
            lv_obj_set_style_flex_main_place(tile, LV_FLEX_ALIGN_SPACE_EVENLY, LV_PART_MAIN | LV_STATE_DEFAULT);
            center_labels_recursive(tile);
        }
    }

    // Rework the SAVE page into two independent columns: [SD + Settings] on
    // the left, [WiFi + Time] on the right. In the original row-based layout
    // the taller WiFi box dictated the first row's height, leaving a gap
    // between the (shorter) SD box and the Settings box below it. The
    // generated row containers are repurposed as columns and the boxes are
    // re-parented at runtime. pad_bottom 30 keeps content above the dots.
    if (objects.container_save != NULL && objects.container_sd_wifi != NULL &&
        objects.container_settings != NULL && objects.container_sd != NULL &&
        objects.container_wifi != NULL && objects.container_bl != NULL &&
        objects.container_rtc != NULL) {
        lv_obj_set_style_flex_flow(objects.container_save, LV_FLEX_FLOW_ROW, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_flex_main_place(objects.container_save, LV_FLEX_ALIGN_SPACE_EVENLY,
                                         LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_bottom(objects.container_save, 30, LV_PART_MAIN | LV_STATE_DEFAULT);

        // Left column: SD (content-sized) + Settings (fills the rest).
        lv_obj_t *col_left = objects.container_sd_wifi;
        lv_obj_set_size(col_left, LV_PCT(50), LV_PCT(100));
        lv_obj_set_style_flex_flow(col_left, LV_FLEX_FLOW_COLUMN, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_row(col_left, 10, LV_PART_MAIN | LV_STATE_DEFAULT);

        // Right column: WiFi (fixed) + Time (fills the rest).
        lv_obj_t *col_right = objects.container_settings;
        lv_obj_set_size(col_right, LV_PCT(48), LV_PCT(100));
        lv_obj_set_flex_grow(col_right, 0);
        lv_obj_set_style_flex_flow(col_right, LV_FLEX_FLOW_COLUMN, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_row(col_right, 10, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_set_parent(objects.container_bl, col_left);
        lv_obj_set_parent(objects.container_wifi, col_right);
        lv_obj_move_to_index(objects.container_wifi, 0);

        lv_obj_set_size(objects.container_sd, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_size(objects.container_bl, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(objects.container_bl, 1);
        lv_obj_set_size(objects.container_wifi, LV_PCT(100), 260);
        lv_obj_set_size(objects.container_rtc, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(objects.container_rtc, 1);
    }

    // Show which trained model is compiled in (AI_MODEL_VARIANT, ai_model.c).
    if (objects.label_ai_model != NULL) {
        char buf[48];
        snprintf(buf, sizeof(buf), "Model: %s", ai_model_get_variant_name());
        lv_label_set_text(objects.label_ai_model, buf);
    }

    // Dot indicator overlay at the bottom of the screen.
    lv_obj_t *dots = lv_obj_create(objects.screen_main);
    lv_obj_remove_style_all(dots);
    lv_obj_set_size(dots, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(dots, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -8);

    for (uint32_t i = 0; i < tab_count; i++) {
        lv_obj_t *dot = lv_obj_create(dots);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 10, 10);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        s_page_dots[i] = dot;
    }

    lv_obj_add_event_cb(tv, tabview_page_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    update_page_dots(lv_tabview_get_tab_act(tv));
}

static void update_status_task(lv_timer_t * timer) {
    if (sd_card_is_recording()) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Saved: %lu", sd_card_get_saved_records());
        lv_label_set_text(objects.label_status, buf);
    }

    update_wifi_status_label();
    update_time_label();
    update_time_sync_label();
}

void action_click_save(lv_event_t * e) {
    if (!s_bl_user_control) {
        lv_label_set_text(objects.label_status, "Status: ignition mode");
        return;
    }

    ESP_LOGI(TAG, "action_click_save triggered (Stop recording)");
    sd_card_stop_recording();
    
    // Disable Save button
    lv_obj_add_state(objects.button_save, LV_STATE_DISABLED);
    // Enable New File button
    lv_obj_clear_state(objects.button_new__file, LV_STATE_DISABLED);
    
    char buf[64];
    snprintf(buf, sizeof(buf), "Status: saved %lu records", sd_card_get_saved_records());
    lv_label_set_text(objects.label_status, buf);
    lv_label_set_text(objects.label_file, "File: -");
}

void action_click_new_file(lv_event_t * e) {
    if (!s_bl_user_control) {
        lv_label_set_text(objects.label_status, "Status: ignition mode");
        return;
    }

    ESP_LOGI(TAG, "action_click_new_file triggered");
    if (objects.panel_new_file != NULL) {
        lv_obj_clear_flag(objects.panel_new_file, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(objects.textarea_filename, "");
        focus_filename_input();
    }
}

void action_click_start(lv_event_t * e) {
    if (!s_bl_user_control) {
        lv_label_set_text(objects.label_status, "Status: ignition mode");
        return;
    }

    ESP_LOGI(TAG, "action_click_start triggered");
    
    const char * filename = lv_textarea_get_text(objects.textarea_filename);
    if (strlen(filename) > 0) {
        if (sd_card_start_recording(filename) != ESP_OK) {
            lv_label_set_text(objects.label_status, "Status: start failed");
            return;
        }
        
        // Hide panel
        if (objects.panel_new_file != NULL) {
            lv_obj_add_flag(objects.panel_new_file, LV_OBJ_FLAG_HIDDEN);
        }
        
        // Update labels 
        char buf[156];
        snprintf(buf, sizeof(buf), "File: %s", filename);
        lv_label_set_text(objects.label_file, buf);
        lv_label_set_text(objects.label_status, "Status: recording...");
        
        // Button handling
        lv_obj_clear_state(objects.button_save, LV_STATE_DISABLED);
        lv_obj_add_state(objects.button_new__file, LV_STATE_DISABLED);
        
        static bool status_timer_created = false;
        if (!status_timer_created) {
            lv_timer_create(update_status_task, 5000, NULL);
            status_timer_created = true;
        }
    }
}

void action_click_cancel(lv_event_t * e) {
    ESP_LOGI(TAG, "action_click_cancel triggered");
    // Hide panel
    if (objects.panel_new_file != NULL) {
        lv_obj_add_flag(objects.panel_new_file, LV_OBJ_FLAG_HIDDEN);
    }
}

void action_click_filename(lv_event_t * e) {
    ESP_LOGI(TAG, "action_click_filename triggered");
    focus_filename_input();
}

void action_pressed_fuel_consumption(lv_event_t * e) {
    ESP_LOGI(TAG, "Fuel consumption panel pressed");
    press_start_time = xTaskGetTickCount();
}

void action_released_fuel_consumption(lv_event_t * e) {
    ESP_LOGI(TAG, "Fuel consumption panel released");
    
    TickType_t press_duration = xTaskGetTickCount() - press_start_time;

    if (press_duration >= pdMS_TO_TICKS(5000)) {
        ESP_LOGI(TAG, "Fuel consumption long press detected (5s) -> Resetting");
        can_app_reset_fuel_consumptions();
    }

    press_start_time = 0; // reset timer
}

static void focus_filename_input(void) {
    if (objects.panel_new_file == NULL || objects.textarea_filename == NULL) {
        return;
    }

    if (objects.keyboard_new_file != NULL) {
        lv_keyboard_set_textarea(objects.keyboard_new_file, objects.textarea_filename);
        lv_obj_add_state(objects.textarea_filename, LV_STATE_FOCUSED);
        lv_event_send(objects.textarea_filename, LV_EVENT_FOCUSED, NULL);
    }
}

static void focus_wifi_input(lv_obj_t *textarea) {
    if (textarea == NULL || objects.keyboard_wi_fi == NULL) {
        return;
    }

    lv_obj_clear_flag(objects.keyboard_wi_fi, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(objects.keyboard_wi_fi, textarea);
    lv_obj_add_state(textarea, LV_STATE_FOCUSED);
    lv_event_send(textarea, LV_EVENT_FOCUSED, NULL);
}

static void hide_wifi_keyboard(void) {
    if (objects.keyboard_wi_fi == NULL) {
        return;
    }

    lv_keyboard_set_textarea(objects.keyboard_wi_fi, NULL);

    if (objects.textarea_wifi_ssid != NULL) {
        lv_obj_clear_state(objects.textarea_wifi_ssid, LV_STATE_FOCUSED);
    }
    if (objects.textarea_wifi_password != NULL) {
        lv_obj_clear_state(objects.textarea_wifi_password, LV_STATE_FOCUSED);
    }

    lv_obj_add_flag(objects.keyboard_wi_fi, LV_OBJ_FLAG_HIDDEN);
}

static void update_wifi_status_label(void) {
    if (objects.label_wifi_status == NULL) {
        return;
    }

    char status[64];
    if (wifi_manager_get_status_text(status, sizeof(status)) == ESP_OK) {
        lv_label_set_text(objects.label_wifi_status, status);
    }
}

static void update_time_label(void) {
    if (objects.label_time == NULL) {
        return;
    }

    char time_text[32];
    if (rtc_manager_format_current_time(time_text, sizeof(time_text)) == ESP_OK) {
        lv_label_set_text(objects.label_time, time_text);
    }
}

static void update_time_sync_label(void) {
    if (objects.label_time_status == NULL) {
        return;
    }

    lv_label_set_text(objects.label_time_status, wifi_manager_is_time_synced() ? "Synced" : "Not synced");
}

static void update_sd_mode_controls(void) {
    if (objects.button_new__file == NULL || objects.button_save == NULL || objects.label_status == NULL) {
        return;
    }

    if (s_bl_user_control) {
        if (sd_card_is_recording()) {
            lv_obj_add_state(objects.button_new__file, LV_STATE_DISABLED);
            lv_obj_clear_state(objects.button_save, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(objects.button_new__file, LV_STATE_DISABLED);
            lv_obj_add_state(objects.button_save, LV_STATE_DISABLED);
        }
    } else {
        lv_obj_add_state(objects.button_new__file, LV_STATE_DISABLED);
        lv_obj_add_state(objects.button_save, LV_STATE_DISABLED);
        if (!sd_card_is_recording()) {
            lv_label_set_text(objects.label_status, "Status: waiting ignition");
        }
    }
}

void ui_save_init_controls(void) {
    if (s_save_controls_initialized) {
        return;
    }

    if (objects.keyboard_wi_fi != NULL) {
        lv_obj_add_flag(objects.keyboard_wi_fi, LV_OBJ_FLAG_HIDDEN);
    }

    if (objects.textarea_wifi_ssid != NULL && objects.textarea_wifi_password != NULL) {
        char ssid[33] = {0};
        char password[65] = {0};
        if (wifi_manager_get_saved_credentials(ssid, sizeof(ssid), password, sizeof(password)) == ESP_OK) {
            lv_textarea_set_text(objects.textarea_wifi_ssid, ssid);
            lv_textarea_set_text(objects.textarea_wifi_password, password);
        }
    }

    update_wifi_status_label();
    update_time_label();
    update_time_sync_label();

    // Restore saved screen brightness (software dimming overlay).
    uint8_t brightness = UI_BRIGHTNESS_MAX;
    nvs_handle_t nvs;
    if (nvs_open(UI_BRIGHTNESS_NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u8(nvs, UI_BRIGHTNESS_NVS_KEY, &brightness);
        nvs_close(nvs);
    }
    if (objects.slider_brightness != NULL) {
        lv_slider_set_value(objects.slider_brightness, brightness, LV_ANIM_OFF);
    }
    apply_brightness(brightness);

    // The keyboard was originally nested inside the WiFi box, which the SAVE
    // page rework shrank to a fixed 260px-tall column half the screen wide -
    // the keyboard (242px tall) got clipped and pinned inside that box
    // instead of the screen. Re-parent it to screen_main (after the dim
    // overlay above so it isn't drawn underneath) and center it along the
    // bottom of the page instead.
    if (objects.keyboard_wi_fi != NULL && objects.screen_main != NULL) {
        lv_obj_set_parent(objects.keyboard_wi_fi, objects.screen_main);
        lv_obj_set_size(objects.keyboard_wi_fi, LV_PCT(95), 242);
        lv_obj_align(objects.keyboard_wi_fi, LV_ALIGN_BOTTOM_MID, 0, -8);
    }

    s_bl_user_control = (objects.switch_bl != NULL) && lv_obj_has_state(objects.switch_bl, LV_STATE_CHECKED);
    sd_card_set_control_mode(s_bl_user_control);
    update_sd_mode_controls();

    s_save_controls_initialized = true;
}

void ui_save_tick(void) {
    update_wifi_status_label();
    update_time_label();
    update_time_sync_label();
    // Fallback sync: the perf label may not exist yet when the pager starts
    // on the AI page (it is created at the first LVGL refresh).
    update_perf_monitor_visibility();

    if (!s_bl_user_control) {
        if (sd_card_is_recording()) {
            char buf[32];
            snprintf(buf, sizeof(buf), "Saved: %lu", sd_card_get_saved_records());
            lv_label_set_text(objects.label_status, buf);
        } else {
            lv_label_set_text(objects.label_status, "Status: waiting ignition");
        }
    }
}

void action_click_ssid(lv_event_t * e) {
    LV_UNUSED(e);
    focus_wifi_input(objects.textarea_wifi_ssid);
}

void action_click_password(lv_event_t * e) {
    LV_UNUSED(e);
    focus_wifi_input(objects.textarea_wifi_password);
}

void action_cancel_keyboard_wifi(lv_event_t * e) {
    LV_UNUSED(e);
    hide_wifi_keyboard();
}

void action_ready_keyboard_wifi(lv_event_t * e) {
    LV_UNUSED(e);
    hide_wifi_keyboard();
}

void action_click_disconnect(lv_event_t * e) {
    LV_UNUSED(e);
    wifi_manager_disconnect();
    hide_wifi_keyboard();
    update_wifi_status_label();
}

void action_click_connect(lv_event_t * e) {
    LV_UNUSED(e);

    const char *ssid = lv_textarea_get_text(objects.textarea_wifi_ssid);
    const char *password = lv_textarea_get_text(objects.textarea_wifi_password);
    esp_err_t ret = wifi_manager_connect(ssid, password);

    if (ret == ESP_OK) {
        lv_label_set_text(objects.label_wifi_status, "Status: connected");
        hide_wifi_keyboard();
    } else {
        lv_label_set_text(objects.label_wifi_status, "Status: connect failed");
    }
}

void action_click_rtc(lv_event_t * e) {
    LV_UNUSED(e);

    esp_err_t ret = wifi_manager_sync_time();
    if (ret == ESP_OK) 
        update_time_label();

    update_time_sync_label();
}

void action_brightness_changed(lv_event_t * e) {
    apply_brightness(lv_slider_get_value(lv_event_get_target(e)));
}

// Persist only on release, not on every drag step (flash wear).
void action_brightness_released(lv_event_t * e) {
    uint8_t value = (uint8_t)lv_slider_get_value(lv_event_get_target(e));

    nvs_handle_t handle;
    if (nvs_open(UI_BRIGHTNESS_NVS_NS, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS for brightness save");
        return;
    }
    if (nvs_set_u8(handle, UI_BRIGHTNESS_NVS_KEY, value) == ESP_OK) {
        nvs_commit(handle);
    }
    nvs_close(handle);
}

static void reset_orchard_msgbox_cb(lv_event_t * e)
{
    lv_obj_t *mbox = lv_event_get_current_target(e);
    const char *btn = lv_msgbox_get_active_btn_text(mbox);

    if (btn != NULL && strcmp(btn, "Reset") == 0) {
        ai_model_reset_orchard();
        ESP_LOGI(TAG, "Orchard reset confirmed");
    }

    lv_msgbox_close(mbox);
}

void action_click_reset_orchard(lv_event_t * e)
{
    LV_UNUSED(e);

    static const char *btns[] = {"Reset", "Cancel", ""};
    lv_obj_t *mbox = lv_msgbox_create(NULL, "Reset orchard",
                                      "Remove all harvested trees from the orchard?",
                                      btns, false);
    lv_obj_add_event_cb(mbox, reset_orchard_msgbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(mbox);
}

static void reset_eco_msgbox_cb(lv_event_t * e)
{
    lv_obj_t *mbox = lv_event_get_current_target(e);
    const char *btn = lv_msgbox_get_active_btn_text(mbox);

    if (btn != NULL && strcmp(btn, "Reset") == 0) {
        eco_stats_reset_all();
        ESP_LOGI(TAG, "ECO stats reset confirmed");
    }

    lv_msgbox_close(mbox);
}

void action_click_reset_eco(lv_event_t * e)
{
    LV_UNUSED(e);

    static const char *btns[] = {"Reset", "Cancel", ""};
    lv_obj_t *mbox = lv_msgbox_create(NULL, "Reset ECO stats",
                                      "Clear all savings, trip/lifetime stats and streaks shown on the ECO page?",
                                      btns, false);
    lv_obj_add_event_cb(mbox, reset_eco_msgbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(mbox);
}

void action_switch_changed(lv_event_t * e) {
    LV_UNUSED(e);

    s_bl_user_control = (objects.switch_bl != NULL) && lv_obj_has_state(objects.switch_bl, LV_STATE_CHECKED);
    sd_card_set_control_mode(s_bl_user_control);
    s_last_short_click_ms = 0;
    update_backlight_mode_label();
    update_sd_mode_controls();

    ESP_LOGI(TAG, "BL control mode: %s", s_bl_user_control ? "User" : "Ignition");
}

void ui_backlight_init_controls(void) {
    register_backlight_short_click_handlers();

    s_bl_user_control = (objects.switch_bl != NULL) && lv_obj_has_state(objects.switch_bl, LV_STATE_CHECKED);
    sd_card_set_control_mode(s_bl_user_control);
    update_backlight_mode_label();
    update_sd_mode_controls();
}

void ui_backlight_sync_ignition(uint8_t ignition) {
    if (s_bl_user_control) {
        return;
    }

    set_backlight_state(ignition != 0);
}

bool ui_backlight_is_user_control(void) {
    return s_bl_user_control;
}

static void action_screen_short_clicked(lv_event_t * e) {
    LV_UNUSED(e);

    uint32_t now_ms = lv_tick_get();
    if (s_last_short_click_ms != 0 && lv_tick_elaps(s_last_short_click_ms) <= 400) {
        set_backlight_state(s_backlight_state != 1);
        s_last_short_click_ms = 0;
        ESP_LOGI(TAG, "Backlight toggled by double click");
    } else {
        s_last_short_click_ms = now_ms;
    }
}

static void register_backlight_short_click_handlers(void) {
    if (s_short_click_handlers_registered) {
        return;
    }

    lv_obj_t *targets[] = {
        objects.screen_main,
        objects.tabview_main,
        objects.tab_can_data,
        objects.tab_save,
        objects.container_can,
        objects.container_save
    };

    size_t target_count = sizeof(targets) / sizeof(targets[0]);
    for (size_t i = 0; i < target_count; i++) {
        if (targets[i] != NULL) {
            lv_obj_add_event_cb(targets[i], action_screen_short_clicked, LV_EVENT_SHORT_CLICKED, NULL);
        }
    }

    s_short_click_handlers_registered = true;
}

static void update_backlight_mode_label(void) {
    if (objects.label_switch != NULL) {
        lv_label_set_text(objects.label_switch, s_bl_user_control ? "BL_control: User" : "BL_control: Ignition");
    }
}

static void set_backlight_state(bool on) {
    if (on) {
        if (s_backlight_state != 1) {
            wavesahre_rgb_lcd_bl_on();
            s_backlight_state = 1;
        }
    } else {
        if (s_backlight_state != 0) {
            wavesahre_rgb_lcd_bl_off();
            s_backlight_state = 0;
        }
    }
}

// Screenshot: hold the top-left corner of any page for 1s to dump the current
// screen to a BMP on the SD card. A transparent trigger sits on the LVGL top
// layer (like the brightness dim overlay) so it works on every tab without
// touching the EEZ Studio-generated screens.
#define UI_SCREENSHOT_HOLD_MS      1000
#define UI_SCREENSHOT_CORNER_SIZE  90
#define SCREENSHOT_DIR             MOUNT_POINT "/snapshots"

static TickType_t s_screenshot_press_start = 0;

#pragma pack(push, 1)
typedef struct {
    uint16_t type;
    uint32_t size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;
} bmp_file_header_t;

typedef struct {
    uint32_t header_size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bpp;
    uint32_t compression;
    uint32_t image_size;
    int32_t x_ppm;
    int32_t y_ppm;
    uint32_t colors_used;
    uint32_t colors_important;
} bmp_info_header_t;
#pragma pack(pop)

static esp_err_t screenshot_build_filename(char *buffer, size_t buffer_size) {
    struct tm rtc_time;
    if (rtc_manager_read_time(&rtc_time) == ESP_OK && rtc_manager_time_is_valid(&rtc_time)) {
        int written = snprintf(buffer, buffer_size, "%s/screenshot_%04d%02d%02d_%02d%02d%02d.bmp",
                               SCREENSHOT_DIR,
                               rtc_time.tm_year + 1900, rtc_time.tm_mon + 1, rtc_time.tm_mday,
                               rtc_time.tm_hour, rtc_time.tm_min, rtc_time.tm_sec);
        return (written > 0 && (size_t)written < buffer_size) ? ESP_OK : ESP_ERR_INVALID_SIZE;
    }

    uint32_t uptime_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    int written = snprintf(buffer, buffer_size, "%s/screenshot_%010lu.bmp", SCREENSHOT_DIR, (unsigned long)uptime_ms);
    return (written > 0 && (size_t)written < buffer_size) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

// Converts the RGB565 snapshot to a 24-bit uncompressed BMP (bottom-up rows)
// for maximum viewer compatibility.
static esp_err_t screenshot_write_bmp(const char *filepath, const lv_img_dsc_t *snap) {
    uint32_t width = snap->header.w;
    uint32_t height = snap->header.h;
    uint32_t row_bytes = width * 3;
    uint32_t padding = (4 - (row_bytes % 4)) % 4;
    uint32_t image_size = (row_bytes + padding) * height;

    bmp_file_header_t file_header = {
        .type = 0x4D42, // "BM"
        .size = (uint32_t)(sizeof(bmp_file_header_t) + sizeof(bmp_info_header_t) + image_size),
        .reserved1 = 0,
        .reserved2 = 0,
        .offset = (uint32_t)(sizeof(bmp_file_header_t) + sizeof(bmp_info_header_t)),
    };

    bmp_info_header_t info_header = {
        .header_size = sizeof(bmp_info_header_t),
        .width = (int32_t)width,
        .height = (int32_t)height,
        .planes = 1,
        .bpp = 24,
        .compression = 0,
        .image_size = image_size,
        .x_ppm = 0,
        .y_ppm = 0,
        .colors_used = 0,
        .colors_important = 0,
    };

    FILE *f = fopen(filepath, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open %s for screenshot (errno=%d)", filepath, errno);
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_OK;
    if (fwrite(&file_header, sizeof(file_header), 1, f) != 1 ||
        fwrite(&info_header, sizeof(info_header), 1, f) != 1) {
        ret = ESP_FAIL;
    }

    if (ret == ESP_OK) {
        uint8_t *row_buf = malloc(row_bytes + padding);
        if (row_buf == NULL) {
            ret = ESP_ERR_NO_MEM;
        } else {
            memset(row_buf + row_bytes, 0, padding);
            const uint16_t *pixels = (const uint16_t *)snap->data;

            // BMP rows are stored bottom-up; the snapshot buffer is top-down.
            for (int32_t y = (int32_t)height - 1; y >= 0 && ret == ESP_OK; y--) {
                const uint16_t *src_row = pixels + (uint32_t)y * width;
                for (uint32_t x = 0; x < width; x++) {
                    uint16_t px = src_row[x];
                    uint8_t r5 = (px >> 11) & 0x1F;
                    uint8_t g6 = (px >> 5) & 0x3F;
                    uint8_t b5 = px & 0x1F;
                    row_buf[x * 3 + 0] = (uint8_t)((b5 << 3) | (b5 >> 2));
                    row_buf[x * 3 + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));
                    row_buf[x * 3 + 2] = (uint8_t)((r5 << 3) | (r5 >> 2));
                }
                if (fwrite(row_buf, row_bytes + padding, 1, f) != 1) {
                    ret = ESP_FAIL;
                }
            }
            free(row_buf);
        }
    }

    fclose(f);
    if (ret != ESP_OK) {
        remove(filepath);
    }
    return ret;
}

static esp_err_t screenshot_capture_and_save(char *out_path, size_t out_path_size) {
    struct stat st;
    if (stat(SCREENSHOT_DIR, &st) != 0 && mkdir(SCREENSHOT_DIR, 0775) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "Failed to create %s (errno=%d)", SCREENSHOT_DIR, errno);
        return ESP_FAIL;
    }

    char filepath[192];
    if (screenshot_build_filename(filepath, sizeof(filepath)) != ESP_OK) {
        return ESP_FAIL;
    }

    lv_img_dsc_t *snap = lv_snapshot_take(lv_scr_act(), LV_IMG_CF_TRUE_COLOR);
    if (snap == NULL) {
        ESP_LOGE(TAG, "Failed to capture screenshot snapshot");
        return ESP_FAIL;
    }

    esp_err_t ret = screenshot_write_bmp(filepath, snap);
    lv_snapshot_free(snap);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Screenshot saved: %s", filepath);
        if (out_path != NULL) {
            snprintf(out_path, out_path_size, "%s", filepath);
        }
    }
    return ret;
}

static void screenshot_toast_del_cb(lv_timer_t *timer) {
    lv_obj_del((lv_obj_t *)timer->user_data);
}

static void screenshot_show_toast(const char *text) {
    lv_obj_t *toast = lv_label_create(lv_layer_top());
    lv_label_set_text(toast, text);
    lv_obj_set_style_bg_color(toast, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(toast, LV_OPA_70, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(toast, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(toast, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(toast, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(toast, LV_ALIGN_TOP_MID, 0, 10);

    lv_timer_t *timer = lv_timer_create(screenshot_toast_del_cb, 1500, toast);
    lv_timer_set_repeat_count(timer, 1);
}

static void screenshot_corner_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        s_screenshot_press_start = xTaskGetTickCount();
    } else if (code == LV_EVENT_PRESS_LOST) {
        s_screenshot_press_start = 0;
    } else if (code == LV_EVENT_RELEASED) {
        if (s_screenshot_press_start == 0) {
            return;
        }
        TickType_t held = xTaskGetTickCount() - s_screenshot_press_start;
        s_screenshot_press_start = 0;

        if (held >= pdMS_TO_TICKS(UI_SCREENSHOT_HOLD_MS)) {
            esp_err_t ret = screenshot_capture_and_save(NULL, 0);
            screenshot_show_toast(ret == ESP_OK ? "Screenshot saved" : "Screenshot failed");
        }
    }
}

void ui_screenshot_init(void) {
    lv_obj_t *corner = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(corner);
    lv_obj_set_size(corner, UI_SCREENSHOT_CORNER_SIZE, UI_SCREENSHOT_CORNER_SIZE);
    lv_obj_set_pos(corner, 0, 0);
    lv_obj_clear_flag(corner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(corner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(corner, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(corner, screenshot_corner_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(corner, screenshot_corner_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(corner, screenshot_corner_event_cb, LV_EVENT_PRESS_LOST, NULL);
}