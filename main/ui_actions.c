#include "ui/actions.h"
#include "ui/screens.h"
#include "esp_log.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "can.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "sd_card.h"
#include "wifi_manager.h"
#include "rtc_manager.h"

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

    s_bl_user_control = (objects.switch_bl != NULL) && lv_obj_has_state(objects.switch_bl, LV_STATE_CHECKED);
    sd_card_set_control_mode(s_bl_user_control);
    update_sd_mode_controls();

    s_save_controls_initialized = true;
}

void ui_save_tick(void) {
    update_wifi_status_label();
    update_time_label();
    update_time_sync_label();

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