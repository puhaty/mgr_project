#include "wifi_manager.h"

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "rtc_manager.h"

#include <string.h>

static const char *TAG = "WIFI_MANAGER";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static EventGroupHandle_t s_wifi_event_group;
static bool s_initialized = false;
static bool s_connected = false;
static bool s_disconnect_requested = false;
static bool s_time_synced = false;
static char s_last_ssid[33] = "";
static char s_last_status[64] = "Status: not connected";

static void update_status_text(const char *text)
{
    if (text == NULL) {
        return;
    }

    snprintf(s_last_status, sizeof(s_last_status), "%s", text);
}

static esp_err_t save_credentials(const char *ssid, const char *password)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open("wifi", NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_str(handle, "ssid", ssid);
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, "password", password);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);
    return ret;
}

static esp_err_t load_credentials(char *ssid, size_t ssid_size, char *password, size_t password_size)
{
    if (ssid == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open("wifi", NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    size_t ssid_len = ssid_size;
    size_t password_len = password_size;
    ret = nvs_get_str(handle, "ssid", ssid, &ssid_len);
    if (ret == ESP_OK) {
        ret = nvs_get_str(handle, "password", password, &password_len);
    }

    nvs_close(handle);
    return ret;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        // Intentionally do not auto-connect here.
        // Connection attempts are started explicitly from wifi_manager_connect().
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        update_status_text("Status: not connected");
        if (!s_disconnect_requested) {
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        update_status_text("Status: connected");
    }
}

esp_err_t wifi_manager_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_initialized = true;

    char ssid[sizeof(s_last_ssid)] = {0};
    char password[65] = {0};
    if (load_credentials(ssid, sizeof(ssid), password, sizeof(password)) == ESP_OK && ssid[0] != '\0') {
        esp_err_t connect_ret = wifi_manager_connect(ssid, password);
        if (connect_ret == ESP_OK) {
            ESP_LOGI(TAG, "Auto-connect from NVS succeeded");
        } else {
            ESP_LOGW(TAG, "Auto-connect from NVS failed: %s", esp_err_to_name(connect_ret));
        }
    }

    return ESP_OK;
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password != NULL) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_config.sta.threshold.authmode = (password != NULL && password[0] != '\0') ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    s_disconnect_requested = false;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    // Ensure previous attempt is canceled; ESP_ERR_WIFI_NOT_CONNECT is acceptable.
    esp_err_t ret = esp_wifi_disconnect();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_CONNECT) {
        ESP_LOGW(TAG, "esp_wifi_disconnect failed: %s", esp_err_to_name(ret));
    }

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(ret));
        update_status_text("Status: config failed");
        return ret;
    }

    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(ret));
        update_status_text("Status: connect failed");
        return ret;
    }

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
    if ((bits & WIFI_CONNECTED_BIT) != 0) {
        snprintf(s_last_ssid, sizeof(s_last_ssid), "%s", ssid);
        update_status_text("Status: connected");
        save_credentials(ssid, password != NULL ? password : "");
        return ESP_OK;
    }

    update_status_text("Status: not connected");
    return ESP_FAIL;
}

esp_err_t wifi_manager_disconnect(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_disconnect_requested = true;
    s_connected = false;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    esp_err_t ret = esp_wifi_disconnect();
    update_status_text("Status: not connected");
    return ret;
}

bool wifi_manager_is_connected(void)
{
    return s_connected;
}

bool wifi_manager_is_time_synced(void)
{
    return s_time_synced;
}

esp_err_t wifi_manager_get_saved_credentials(char *ssid, size_t ssid_size, char *password, size_t password_size)
{
    return load_credentials(ssid, ssid_size, password, password_size);
}

esp_err_t wifi_manager_get_status_text(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(buffer, buffer_size, "%s", s_last_status);
    return ESP_OK;
}

esp_err_t wifi_manager_sync_time(void)
{
    if (!s_connected) {
        s_time_synced = false;
        return ESP_ERR_INVALID_STATE;
    }

    esp_sntp_stop();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "tempus1.gum.gov.pl");
    esp_sntp_init();

    time_t now = 0;
    int retry = 0;
    const int retry_count = 20;
    bool synced = false;

    while (retry < retry_count) {
        if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            synced = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
        retry++;
    }

    esp_sntp_stop();

    if (!synced) {
        update_status_text("Status: time sync failed");
        s_time_synced = false;
        return ESP_ERR_TIMEOUT;
    }

    struct tm timeinfo = {0};
    time(&now);
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_year < (2024 - 1900)) {
        update_status_text("Status: time sync failed");
        s_time_synced = false;
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t rtc_ret = rtc_manager_sync_rtc_from_system();
    if (rtc_ret != ESP_OK) {
        update_status_text("Status: RTC sync failed");
        s_time_synced = false;
        return rtc_ret;
    }
    
    s_time_synced = true;
    return ESP_OK;
}