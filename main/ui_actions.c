#include "ui/actions.h"
#include "ui/screens.h"
#include "esp_log.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "can.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "sd_card.h"

static const char *TAG = "ui_actions";

static void focus_filename_input(void);
static void set_backlight_state(bool on);
static void update_backlight_mode_label(void);
static void register_backlight_short_click_handlers(void);
static void action_screen_short_clicked(lv_event_t * e);
esp_err_t wavesahre_rgb_lcd_bl_on(void);
esp_err_t wavesahre_rgb_lcd_bl_off(void);

// Adding a global (static) pointer to the keyboard, 
// because the generator did not assign it to a generated variable.
static lv_obj_t * kb_instance = NULL;
static TickType_t press_start_time = 0;

// Variables for handling SD card writing
static volatile bool is_recording = false;
static char current_filename[128] = "";
static uint32_t saved_records = 0;
static TaskHandle_t sd_logger_handle = NULL;
static bool s_bl_user_control = false;
static int8_t s_backlight_state = -1; // -1 unknown, 0 off, 1 on
static uint32_t s_last_short_click_ms = 0;
static bool s_short_click_handlers_registered = false;

void sd_logger_task(void *pvParameters) {
    while (1) {
        if (is_recording) {
            can_data_t *data = can_app_get_data();
            
            // Only if ignition is on
            if (data->ignition) {
                char filepath[140];
                snprintf(filepath, sizeof(filepath), "%s/%s.csv", MOUNT_POINT, current_filename);
                // "a" - append to file, safe after power failure
                FILE *f = fopen(filepath, "a");
                if (f != NULL) {
                    if (saved_records == 0) {
                        fprintf(f, "timestamp,ignition,speed,rpm,throttle,brake,fuel_level,fuel_cons,distance,range,oil_temp,esp\n");
                    }
                    fprintf(f, "%lu,%u,%u,%lu,%u,%u,%u,%.2f,%lu,%u,%d,%u\n",
                            data->timestamp, data->ignition, data->speed, data->rpm,
                            data->throttle_pedal, data->brake_pedal, data->fuel_level,
                            data->fuel_consumption, data->t_distance, data->range,
                            data->oil_temp, data->esp_stat);
                    fclose(f);
                    saved_records++;
                } else {
                    ESP_LOGE(TAG, "Failed to open file %s for writing", filepath);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // every 100ms
    }
}

static void update_status_task(lv_timer_t * timer) {
    if (is_recording) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Saved: %lu", saved_records);
        lv_label_set_text(objects.label_status, buf);
    }
}

void action_click_save(lv_event_t * e) {
    ESP_LOGI(TAG, "action_click_save triggered (Stop recording)");
    is_recording = false;
    
    // Disable Save button
    lv_obj_add_state(objects.button_save, LV_STATE_DISABLED);
    // Enable New File button
    lv_obj_clear_state(objects.button_new__file, LV_STATE_DISABLED);
    
    char buf[64];
    snprintf(buf, sizeof(buf), "Status: saved %lu records", saved_records);
    lv_label_set_text(objects.label_status, buf);
    lv_label_set_text(objects.label_file, "File: -");
}

void action_click_new_file(lv_event_t * e) {
    ESP_LOGI(TAG, "action_click_new_file triggered");
    if (objects.panel_new_file != NULL) {
        lv_obj_clear_flag(objects.panel_new_file, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(objects.textarea_filename, "");
        focus_filename_input();
    }
}

void action_click_start(lv_event_t * e) {
    ESP_LOGI(TAG, "action_click_start triggered");
    
    const char * filename = lv_textarea_get_text(objects.textarea_filename);
    if(strlen(filename) > 0) {
        strncpy(current_filename, filename, sizeof(current_filename)-1);
        current_filename[sizeof(current_filename)-1] = '\0';
        
        saved_records = 0;
        is_recording = true;
        
        // Hide panel
        if (objects.panel_new_file != NULL) {
            lv_obj_add_flag(objects.panel_new_file, LV_OBJ_FLAG_HIDDEN);
        }
        
        // Update labels 
        char buf[156];
        snprintf(buf, sizeof(buf), "File: %s", current_filename);
        lv_label_set_text(objects.label_file, buf);
        lv_label_set_text(objects.label_status, "Status: recording...");
        
        // Button handling
        lv_obj_clear_state(objects.button_save, LV_STATE_DISABLED);
        lv_obj_add_state(objects.button_new__file, LV_STATE_DISABLED);
        
        // Start logger task once
        if(sd_logger_handle == NULL) {
            xTaskCreate(sd_logger_task, "sd_logger", 4096, NULL, 5, &sd_logger_handle);
            lv_timer_create(update_status_task, 5000, NULL);
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

    if (kb_instance == NULL) {
        uint32_t child_cnt = lv_obj_get_child_cnt(objects.panel_new_file);
        for (uint32_t i = 0; i < child_cnt; i++) {
            lv_obj_t *child = lv_obj_get_child(objects.panel_new_file, i);
            if (lv_obj_check_type(child, &lv_keyboard_class)) {
                kb_instance = child;
                break;
            }
        }
    }

    if (kb_instance != NULL) {
        lv_keyboard_set_textarea(kb_instance, objects.textarea_filename);
        lv_obj_add_state(objects.textarea_filename, LV_STATE_FOCUSED);
        lv_event_send(objects.textarea_filename, LV_EVENT_FOCUSED, NULL);
    }
}

void action_switch_changed(lv_event_t * e) {
    LV_UNUSED(e);

    s_bl_user_control = (objects.switch_bl != NULL) && lv_obj_has_state(objects.switch_bl, LV_STATE_CHECKED);
    s_last_short_click_ms = 0;
    update_backlight_mode_label();

    ESP_LOGI(TAG, "BL control mode: %s", s_bl_user_control ? "User" : "Ignition");
}

void ui_backlight_init_controls(void) {
    register_backlight_short_click_handlers();

    s_bl_user_control = (objects.switch_bl != NULL) && lv_obj_has_state(objects.switch_bl, LV_STATE_CHECKED);
    update_backlight_mode_label();
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