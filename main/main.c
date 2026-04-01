/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "waveshare_rgb_lcd_port.h"
#include "ui/ui.h"
#include "ui/screens.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_sleep.h"
#include "can.h"
#include "sd_card.h"

static void update_ui_can_data(can_data_t* data);
static void maybe_enter_deep_sleep_on_can_timeout(void);
void ui_backlight_init_controls(void);
bool ui_backlight_is_user_control(void);

static const char *TAG = "MAIN";
static const uint32_t CAN_INACTIVITY_DEEP_SLEEP_MS = 20000;
static const int CAN_WAKEUP_LEVEL = 0; // CAN dominant bit drives RX low

void app_main()
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    waveshare_esp32_s3_rgb_lcd_init(); // Initialize the Waveshare ESP32-S3 RGB LCD

    // Initialize SD card handler
    if (waveshare_sd_card_init() == ESP_OK) {
        ESP_LOGI(TAG, "SD Card Init Success!");
    } else {
        ESP_LOGE(TAG, "SD Card Init Failed!");
    }
    
    if(can_app_init(50) != ESP_OK) {
        ESP_LOGE(TAG, "CAN Init Failed!");
    } else {
        ESP_LOGI(TAG, "CAN Init Success!");
    }

    // Initialize UI once under LVGL mutex.
    if (lvgl_port_lock(-1)) {
        ui_init();
        ui_backlight_init_controls();
        lvgl_port_unlock();
    }

    TickType_t xLastUITime = xTaskGetTickCount();
    TickType_t xLastCANTime = xLastUITime;
    const TickType_t xUI_Delay = pdMS_TO_TICKS(16);
    const TickType_t xCAN_Delay = pdMS_TO_TICKS(500);

    // Refresh EEZ/LVGL UI periodically.
    while (1) {
        can_app_receive(); 
        maybe_enter_deep_sleep_on_can_timeout();

        TickType_t xNow = xTaskGetTickCount();

        if (xNow - xLastCANTime >= xCAN_Delay) {
            can_data_t* data = can_app_get_data();
            update_ui_can_data(data);
            xLastCANTime = xNow;
        }

        if (xNow - xLastUITime >= xUI_Delay) {
            if (lvgl_port_lock(-1)) { 
                ui_tick();
                lvgl_port_unlock();
            }
            xLastUITime = xNow;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void maybe_enter_deep_sleep_on_can_timeout(void) {
    if (ui_backlight_is_user_control()) {
        return;
    }

    uint32_t idle_ms = can_app_get_idle_time_ms();
    if (idle_ms < CAN_INACTIVITY_DEEP_SLEEP_MS) {
        return;
    }

    ESP_LOGI(TAG, "No CAN traffic for %lu ms (Ignition mode). Entering deep sleep.", (unsigned long)idle_ms);

    wavesahre_rgb_lcd_bl_off();

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_err_t err = esp_sleep_enable_ext0_wakeup((gpio_num_t)CAN_RX_PIN, CAN_WAKEUP_LEVEL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure EXT0 wakeup on CAN RX GPIO %d: %s", CAN_RX_PIN, esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Wake source: CAN RX GPIO %d, level %d", CAN_RX_PIN, CAN_WAKEUP_LEVEL);
    vTaskDelay(pdMS_TO_TICKS(20));
    esp_deep_sleep_start();
}

static void update_ui_can_data(can_data_t* data) {
    // ESP_LOGI(TAG, "CAN Data - Timestamp: %" PRIu32 ", Ignition: %d, Speed: %d km/h, RPM: %" PRIu32 ", Throttle: %d%%, Brake: %d%%, Fuel Level: %d%%, Fuel Consumption: %" PRIu32 " l, ESP Status: %d, T_Distance: %" PRIu32 " km, Range: %d km, Oil Temp: %d C", 
        // data->timestamp, data->ignition, data->speed, data->rpm, data->throttle_pedal, data->brake_pedal, data->fuel_level, data->fuel_consumption, data->esp_stat, data->t_distance, data->range, data->oil_temp);
    
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f ml", data->fuel_consumption);

    if (lvgl_port_lock(-1)) {
        lv_label_set_text_fmt(objects.label_speed, "%d km/h", data->speed);
        lv_label_set_text_fmt(objects.label_rpm, "%" PRIu32 " rpm/min", data->rpm);
        lv_label_set_text_fmt(objects.label_ignition, "%s", data->ignition ? "ON" : "OFF");
        lv_label_set_text_fmt(objects.label_acc, "%d %%", data->throttle_pedal);
        lv_label_set_text_fmt(objects.label_b_pedal, "%d %%", data->brake_pedal);
        lv_label_set_text_fmt(objects.label_f_level, "%d %%", data->fuel_level);
        lv_label_set_text_fmt(objects.label_fuel_consumption, "%s", buf);
        lv_label_set_text_fmt(objects.label_oil_temp, "%d *C", data->oil_temp);
        lv_label_set_text_fmt(objects.label_t_distance, "%" PRIu32 " km", data->t_distance);
        lv_label_set_text_fmt(objects.label_range, "%d km", data->range);

        const char* esp_str = "ON";
        if ((data->esp_stat & ESP_MASK) == ESP_ON) esp_str = "ON";
        else if ((data->esp_stat & ESP_MASK) == ESP_OFF_LEVEL_1) esp_str = "OFF 1";
        else if ((data->esp_stat & ESP_MASK) == ESP_OFF_LEVEL_2) esp_str = "OFF 2";
        lv_label_set_text(objects.label_esp, esp_str);

        lvgl_port_unlock();
    }
}