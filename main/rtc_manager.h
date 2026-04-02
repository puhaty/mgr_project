#ifndef RTC_MANAGER_H
#define RTC_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include "esp_err.h"

esp_err_t rtc_manager_init(void);
esp_err_t rtc_manager_read_time(struct tm *timeinfo);
esp_err_t rtc_manager_write_time(const struct tm *timeinfo);
esp_err_t rtc_manager_sync_system_from_rtc(void);
esp_err_t rtc_manager_sync_rtc_from_system(void);
bool rtc_manager_time_is_valid(const struct tm *timeinfo);
esp_err_t rtc_manager_format_current_time(char *buffer, size_t buffer_size);

#endif