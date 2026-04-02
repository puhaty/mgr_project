#include "sd_card.h"
#include "can.h"
#include "rtc_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "SD_CARD";

sdmmc_card_t *card;
const char mount_point[] = MOUNT_POINT;
static volatile bool s_is_recording = false;
static char s_current_filename[128] = "";
static uint32_t s_saved_records = 0;
static TaskHandle_t s_sd_logger_handle = NULL;
static bool s_user_control_mode = false;
static uint32_t s_fallback_file_counter = 0;
static bool s_sd_ready = false;
static uint8_t s_consecutive_open_failures = 0;

static esp_err_t sd_card_start_recording_internal(const char *filename);
static esp_err_t sd_card_build_auto_filename(char *buffer, size_t buffer_size);

static void sd_logger_task(void *pvParameters)
{
    (void)pvParameters;

    while (1) {
        if (s_is_recording) {
            can_data_t *data = can_app_get_data();

            // Save only while ignition is on.
            if (data->ignition) {
                char filepath[160];
                snprintf(filepath, sizeof(filepath), "%s/%s.csv", MOUNT_POINT, s_current_filename);

                FILE *f = fopen(filepath, "a");
                if (f != NULL) {
                    s_consecutive_open_failures = 0;
                    if (s_saved_records == 0) {
                        fprintf(f, "timestamp,ignition,speed,rpm,throttle,brake,fuel_level,fuel_cons,distance,range,oil_temp,esp,rear_gear\n");
                    }

                    fprintf(f, "%lu,%u,%u,%lu,%u,%u,%u,%.2f,%lu,%u,%d,%u,%u\n",
                            data->timestamp,
                            data->ignition,
                            data->speed,
                            data->rpm,
                            data->throttle_pedal,
                            data->brake_pedal,
                            data->fuel_level,
                            data->fuel_consumption,
                            data->t_distance,
                            data->range,
                            data->oil_temp,
                            data->esp_stat,
                            data->rear_gear);

                    fclose(f);
                    s_saved_records++;
                } else {
                    int err = errno;
                    s_consecutive_open_failures++;
                    ESP_LOGE(TAG, "Failed to open file %s for writing (errno=%d: %s)", filepath, err, strerror(err));

                    // Prevent infinite error spam when storage is unavailable.
                    if (s_consecutive_open_failures >= 10) {
                        ESP_LOGE(TAG, "Stopping recording after repeated open failures");
                        s_is_recording = false;
                        s_consecutive_open_failures = 0;
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// By default, SD card frequency is initialized to SDMMC_FREQ_DEFAULT (20MHz)
// For setting a specific frequency, use host.max_freq_khz (range 400kHz - 20MHz for SDSPI)
// Example: for fixed frequency of 10MHz, use host.max_freq_khz = 10000;
sdmmc_host_t host = SDSPI_HOST_DEFAULT();

esp_err_t waveshare_sd_card_init()
{
    esp_err_t ret;
    // I2C init was already done by the LCD driver

    // Control CH422G to pull down the CS pin of the SD
    uint8_t write_buf = 0x01;
    i2c_master_write_to_device(I2C_MASTER_NUM, 0x24, &write_buf, 1, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    
    // We must maintain Touch RST (0x02) High and Backlight (0x04) unchanged or High, 
    // whilst keeping SD CS (0x10) Low. Thus 0x2E instead of 0x0A (which disabled BL)
    write_buf = 0x2E;
    i2c_master_write_to_device(I2C_MASTER_NUM, 0x38, &write_buf, 1, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);

    // Options for mounting the filesystem
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
#ifdef CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED
        .format_if_mount_failed = true, // If mount fails, format the card
#else
        .format_if_mount_failed = false, // If mount fails, do not format card
#endif
        .max_files = 5,                   // Maximum number of files
        .allocation_unit_size = 16 * 1024 // Set allocation unit size
    };

    // Initializing SD card
    ESP_LOGW(TAG, "Initializing SD card");

    // Configure SPI bus for SD card configuration
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI, // Set MOSI pin
        .miso_io_num = PIN_NUM_MISO, // Set MISO pin
        .sclk_io_num = PIN_NUM_CLK,  // Set SCLK pin
        .quadwp_io_num = -1,         // Not used
        .quadhd_io_num = -1,         // Not used
        .max_transfer_sz = 4000,     // Maximum transfer size
    };
    // Initialize SPI bus
    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK)
    {
        // Failed to initialize bus
        ESP_LOGW(TAG, "Failed to initialize bus.");
        return ESP_FAIL;
    }

    // Configure SD card slot
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS; // Set CS pin
    slot_config.host_id = host.slot;  // Set host ID

    // Mounting filesystem
    ESP_LOGW(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            // Failed to mount filesystem
            ESP_LOGW(TAG, "Failed to mount filesystem. "
                          "If you want the card to be formatted, set the CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        }
        else
        {
            // Failed to initialize the card
            ESP_LOGW(TAG, "Failed to initialize the card (%s). "
                          "Make sure SD card lines have pull-up resistors in place.",
                     esp_err_to_name(ret));
        }
        s_sd_ready = false;
        return ESP_FAIL;
    }

    // Filesystem mounted
    ESP_LOGW(TAG, "Filesystem mounted");
    s_sd_ready = true;
    return ESP_OK;
}

static esp_err_t sd_card_build_auto_filename(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    struct tm rtc_time;
    if (rtc_manager_read_time(&rtc_time) == ESP_OK && rtc_manager_time_is_valid(&rtc_time)) {
        int written = snprintf(buffer, buffer_size,
                               "%04d%02d%02d_%02d%02d%02d",
                               rtc_time.tm_year + 1900,
                               rtc_time.tm_mon + 1,
                               rtc_time.tm_mday,
                               rtc_time.tm_hour,
                               rtc_time.tm_min,
                               rtc_time.tm_sec);
        if (written <= 0 || (size_t)written >= buffer_size) {
            return ESP_ERR_INVALID_SIZE;
        }
        return ESP_OK;
    }

    uint32_t uptime_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    int written = snprintf(buffer, buffer_size, "rec_%010lu_%03lu",
                           (unsigned long)uptime_ms,
                           (unsigned long)(s_fallback_file_counter++ % 1000));
    if (written <= 0 || (size_t)written >= buffer_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

void sd_card_set_control_mode(bool user_control_mode)
{
    s_user_control_mode = user_control_mode;
}

bool sd_card_is_user_control_mode(void)
{
    return s_user_control_mode;
}

void sd_card_on_ignition_on(void)
{
    if (s_user_control_mode || s_is_recording) {
        return;
    }

    char auto_filename[64] = {0};
    if (sd_card_build_auto_filename(auto_filename, sizeof(auto_filename)) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to build auto filename for ignition recording");
        return;
    }

    esp_err_t ret = sd_card_start_recording_internal(auto_filename);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start ignition recording: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Ignition recording started: %s", auto_filename);
    }
}

void sd_card_on_ignition_off(void)
{
    if (s_user_control_mode) {
        return;
    }

    if (s_is_recording) {
        sd_card_stop_recording();
        ESP_LOGI(TAG, "Ignition recording stopped");
    }
}

static esp_err_t sd_card_start_recording_internal(const char *filename)
{
    if (!s_sd_ready) {
        ESP_LOGE(TAG, "SD card filesystem not ready");
        return ESP_ERR_INVALID_STATE;
    }

    if (filename == NULL || filename[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    size_t copied = snprintf(s_current_filename, sizeof(s_current_filename), "%s", filename);
    if (copied >= sizeof(s_current_filename)) {
        return ESP_ERR_INVALID_SIZE;
    }

    s_saved_records = 0;
    s_is_recording = true;
    s_consecutive_open_failures = 0;

    if (s_sd_logger_handle == NULL) {
        BaseType_t ret = xTaskCreate(sd_logger_task, "sd_logger", 4096, NULL, 5, &s_sd_logger_handle);
        if (ret != pdPASS) {
            s_is_recording = false;
            s_current_filename[0] = '\0';
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

esp_err_t sd_card_start_recording(const char *filename)
{
    return sd_card_start_recording_internal(filename);
}

void sd_card_stop_recording(void)
{
    s_is_recording = false;
    s_consecutive_open_failures = 0;
}

bool sd_card_is_recording(void)
{
    return s_is_recording;
}

uint32_t sd_card_get_saved_records(void)
{
    return s_saved_records;
}