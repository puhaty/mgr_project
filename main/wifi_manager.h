#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_connect(const char *ssid, const char *password);
esp_err_t wifi_manager_disconnect(void);
bool wifi_manager_is_connected(void);
esp_err_t wifi_manager_get_status_text(char *buffer, size_t buffer_size);
esp_err_t wifi_manager_sync_time(void);
bool wifi_manager_is_time_synced(void);
esp_err_t wifi_manager_get_saved_credentials(char *ssid, size_t ssid_size, char *password, size_t password_size);

#endif