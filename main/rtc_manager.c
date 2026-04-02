#include "rtc_manager.h"

#include "esp_log.h"
#include "waveshare_rgb_lcd_port.h"

#include <freertos/FreeRTOS.h>
#include <string.h>
#include <sys/time.h>

#define PCF85063A_ADDRESS 0x51
#define RTC_CTRL_1_ADDR 0x00
#define RTC_SECOND_ADDR 0x04
#define RTC_MINUTE_ADDR 0x05
#define RTC_HOUR_ADDR 0x06
#define RTC_DAY_ADDR 0x07
#define RTC_WDAY_ADDR 0x08
#define RTC_MONTH_ADDR 0x09
#define RTC_YEAR_ADDR 0x0A

#define RTC_CTRL_1_CAP_SEL 0x01

#define YEAR_BASE 1970

static bool s_initialized = false;

static uint8_t dec_to_bcd(int value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static int bcd_to_dec(uint8_t value)
{
    return (int)(((value >> 4) * 10) + (value & 0x0F));
}

static esp_err_t rtc_write_bytes(uint8_t register_addr, const uint8_t *data, size_t length)
{
    uint8_t buffer[16];
    if (length + 1 > sizeof(buffer)) {
        return ESP_ERR_INVALID_SIZE;
    }

    buffer[0] = register_addr;
    memcpy(&buffer[1], data, length);
    return i2c_master_write_to_device(I2C_MASTER_NUM, PCF85063A_ADDRESS, buffer, length + 1, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
}

static esp_err_t rtc_read_bytes(uint8_t register_addr, uint8_t *data, size_t length)
{
    return i2c_master_write_read_device(I2C_MASTER_NUM, PCF85063A_ADDRESS, &register_addr, 1, data, length, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
}

esp_err_t rtc_manager_init(void)
{
    uint8_t value = RTC_CTRL_1_CAP_SEL;
    esp_err_t ret = rtc_write_bytes(RTC_CTRL_1_ADDR, &value, 1);
    if (ret == ESP_OK) {
        s_initialized = true;
    }
    return ret;
}

bool rtc_manager_time_is_valid(const struct tm *timeinfo)
{
    if (timeinfo == NULL) {
        return false;
    }

    int year = timeinfo->tm_year + 1900;
    return year >= 2024 && year <= 2099 &&
           timeinfo->tm_mon >= 0 && timeinfo->tm_mon < 12 &&
           timeinfo->tm_mday >= 1 && timeinfo->tm_mday <= 31 &&
           timeinfo->tm_hour >= 0 && timeinfo->tm_hour < 24 &&
           timeinfo->tm_min >= 0 && timeinfo->tm_min < 60 &&
           timeinfo->tm_sec >= 0 && timeinfo->tm_sec < 60;
}

esp_err_t rtc_manager_read_time(struct tm *timeinfo)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (timeinfo == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data[7] = {0};
    esp_err_t ret = rtc_read_bytes(RTC_SECOND_ADDR, data, sizeof(data));
    if (ret != ESP_OK) {
        return ret;
    }

    memset(timeinfo, 0, sizeof(*timeinfo));
    timeinfo->tm_sec = bcd_to_dec(data[0] & 0x7F);
    timeinfo->tm_min = bcd_to_dec(data[1] & 0x7F);
    timeinfo->tm_hour = bcd_to_dec(data[2] & 0x3F);
    timeinfo->tm_mday = bcd_to_dec(data[3] & 0x3F);
    timeinfo->tm_wday = bcd_to_dec(data[4] & 0x07);
    timeinfo->tm_mon = bcd_to_dec(data[5] & 0x1F) - 1;
    timeinfo->tm_year = bcd_to_dec(data[6]) + (YEAR_BASE - 1900);
    return ESP_OK;
}

esp_err_t rtc_manager_write_time(const struct tm *timeinfo)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!rtc_manager_time_is_valid(timeinfo)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data[7] = {
        dec_to_bcd(timeinfo->tm_sec),
        dec_to_bcd(timeinfo->tm_min),
        dec_to_bcd(timeinfo->tm_hour),
        dec_to_bcd(timeinfo->tm_mday),
        dec_to_bcd(timeinfo->tm_wday),
        dec_to_bcd(timeinfo->tm_mon + 1),
        dec_to_bcd((timeinfo->tm_year + 1900) - YEAR_BASE),
    };

    return rtc_write_bytes(RTC_SECOND_ADDR, data, sizeof(data));
}

esp_err_t rtc_manager_sync_system_from_rtc(void)
{
    struct tm timeinfo;
    esp_err_t ret = rtc_manager_read_time(&timeinfo);
    if (ret != ESP_OK) {
        return ret;
    }

    if (!rtc_manager_time_is_valid(&timeinfo)) {
        return ESP_ERR_INVALID_STATE;
    }

    // Let mktime determine DST based on configured TZ rules.
    timeinfo.tm_isdst = -1;

    struct timeval tv = {
        .tv_sec = mktime(&timeinfo),
        .tv_usec = 0,
    };

    if (tv.tv_sec < 0) {
        return ESP_FAIL;
    }

    return settimeofday(&tv, NULL);
}

esp_err_t rtc_manager_sync_rtc_from_system(void)
{
    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    return rtc_manager_write_time(&timeinfo);
}

esp_err_t rtc_manager_format_current_time(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    struct tm timeinfo;
    esp_err_t ret = rtc_manager_read_time(&timeinfo);
    if (ret != ESP_OK) {
        return ret;
    }

    if (!rtc_manager_time_is_valid(&timeinfo)) {
        return ESP_ERR_INVALID_STATE;
    }

    snprintf(buffer, buffer_size, "%02d.%02d.%04d %02d:%02d",
             timeinfo.tm_mday,
             timeinfo.tm_mon + 1,
             timeinfo.tm_year + 1900,
             timeinfo.tm_hour,
             timeinfo.tm_min);
    return ESP_OK;
}