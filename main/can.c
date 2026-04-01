#include "can.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <inttypes.h>

static const char *TAG = "CAN_APP";

static can_data_t current_can_data = {0};
static bool driver_installed = false;
static float last_saved_fuel = 0;
static uint32_t CAN_fuel_dose = 0; // Temporary variable to accumulate fuel consumption in 30.51 nL/bit
static volatile uint32_t last_can_rx_timestamp_ms = 0;

void can_app_load_fuel_consumption(void);
void can_app_save_fuel_consumption(void);
void ui_backlight_sync_ignition(uint8_t ignition);

// Internal function to map simple integer (kbps) to TWAI driver structures
static esp_err_t get_timing_config(uint32_t baudrate, twai_timing_config_t* t_config) {
    switch(baudrate) {
        case 20: *t_config = (twai_timing_config_t)TWAI_TIMING_CONFIG_20KBITS(); break;     // 20000 bps
        case 50: *t_config = (twai_timing_config_t)TWAI_TIMING_CONFIG_50KBITS(); break;     // 50000 bps        
        case 100: *t_config = (twai_timing_config_t)TWAI_TIMING_CONFIG_100KBITS(); break;   // 100000 bps
        case 125: *t_config = (twai_timing_config_t)TWAI_TIMING_CONFIG_125KBITS(); break;   // 125000 bps
        case 250: *t_config = (twai_timing_config_t)TWAI_TIMING_CONFIG_250KBITS(); break;   // 250000 bps
        case 500: *t_config = (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS(); break;   // 500000 bps
        case 800: *t_config = (twai_timing_config_t)TWAI_TIMING_CONFIG_800KBITS(); break;   // 800000 bps
        case 1000: *t_config = (twai_timing_config_t)TWAI_TIMING_CONFIG_1MBITS();  break;   // 1000000 bps
        default:
            ESP_LOGE(TAG, "Unsupported baudrate: %" PRIu32 " kbps", baudrate);
            return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static void handle_rx_message(twai_message_t message) {
    // log received message for debugging
    // ESP_LOGI(TAG, "Received CAN message: ID=0x%03" PRIX32 ", DLC=%d", message.identifier, message.data_length_code);    
    
    // Switch-case based parsing logic based on CAN Message Identifier
    switch (message.identifier) {
        
        case 0x06214000: { // Ignition Status & Fuel Level
            uint8_t new_ignition = ((uint8_t)message.data[2] & 0x40) ? 1 : 0;
            
            if (current_can_data.ignition == 0 && new_ignition == 1) {
                can_app_load_fuel_consumption();
                ui_backlight_sync_ignition(new_ignition);
            } else if (current_can_data.ignition == 1 && new_ignition == 0) {
                can_app_save_fuel_consumption();
                ui_backlight_sync_ignition(new_ignition);
            }
            
            current_can_data.ignition = new_ignition;
            
            uint8_t raw_fuel_data = (uint8_t)message.data[5] & 0x7F;
            if (raw_fuel_data != 0x7F)
                current_can_data.fuel_level = (uint8_t)raw_fuel_data;
            break;
        }

        case 0x04394000: { // Vehicle Speed
            if (current_can_data.ignition) {
                uint16_t raw_data = (uint16_t)((message.data[0] << 8) | message.data[1]);
                if (raw_data != 0xFFFF)
                    current_can_data.speed = (uint8_t)(raw_data >> 4);
            }   
            else {
                current_can_data.speed = 0;                
            }         
            break;
        }

        case 0x04394100: { // Brake Pedal Position
            if (message.data[3] != 0xFF)
                current_can_data.throttle_pedal = (uint8_t)message.data[3];
            break;
        }

        case 0x04214001: { // RPM & Fuel Consumption & Oil Temperature``
            if (current_can_data.ignition) {
                uint16_t raw_data = (uint16_t)((message.data[6] << 8) | message.data[5]);
                if (raw_data != 0xFFFF)
                    current_can_data.rpm = (uint16_t)(raw_data >> 3);

                raw_data = (uint16_t)((message.data[4] << 8) | message.data[5]);
                if (raw_data != 0xFFFF) {
                    // 30,51 nL/bit
                    CAN_fuel_dose += raw_data;
                    
                    // if CAN_fuel_dose exceeds 1 mL (1000000 nL), increment fuel_consumption
                    if (CAN_fuel_dose >= 32777) { // 32776 bits * 30.51 nL/bit ≈ 1 mL
                        CAN_fuel_dose *= 15621;
                        CAN_fuel_dose >>= 9;
                        current_can_data.fuel_consumption += (float) (CAN_fuel_dose / 1000000.0f);
                        CAN_fuel_dose = 0; // reset accumulator
                    }

                }
            }
            else {
                current_can_data.rpm = 0;                
            }
            current_can_data.oil_temp = (int8_t) (message.data[3] - 40);
            break;
        }

        case 0x0C014003: { // Total Distance [km] & Range[km]            
            uint32_t raw_data= (__builtin_bswap32(*(uint32_t*)message.data)) & 0xFFFFF;
            if (raw_data != 0xFFFFF)
                current_can_data.t_distance = raw_data;

            raw_data = (uint16_t)(((message.data[4] << 8) | message.data[5]) & 0x7FFF);
            if (raw_data != 0x7FFF)
                current_can_data.range = raw_data;
            break;
        }


        case 0x03029000: { // Brake Pedal Position
            uint16_t raw_data = (uint16_t)(((message.data[5] << 8) | message.data[6]) & 0xFFF);
            if (raw_data != 0xFFF)
                current_can_data.brake_pedal = (uint16_t)raw_data;
            break;
        }

        case 0x06314000: { // ESP Status
            uint8_t raw_data = (uint8_t)message.data[5] & 0xFF;    
            if (raw_data != 0xFF)
                current_can_data.esp_stat = raw_data & 0x03;
            else
                current_can_data.esp_stat = 0;
            break;
        }

        default:
            return; 
    }
    
    // Update timestamp indicating new valid data arrived (update only if known ID matched)
    current_can_data.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
}

void can_app_load_fuel_consumption(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        uint32_t saved_fuel_u32 = 0;
        err = nvs_get_u32(my_handle, "fuel_cons", &saved_fuel_u32);
        if (err == ESP_OK) {
            float saved_fuel;
            memcpy(&saved_fuel, &saved_fuel_u32, sizeof(float)); // Interpret the uint32_t bits as float
            current_can_data.fuel_consumption = saved_fuel;
            last_saved_fuel = saved_fuel; // Ustaw tak, by nie dublować zapisu
            ESP_LOGI(TAG, "Loaded fuel consumption from NVS: %.3f", saved_fuel);
        }
        nvs_close(my_handle);
    } else {
        ESP_LOGI(TAG, "No NVS data found for fuel consumption (first boot or formatted).");
    }
}

void can_app_save_fuel_consumption(void) {
    if (CAN_fuel_dose > 0) {
        CAN_fuel_dose *= 15621;
        CAN_fuel_dose >>= 9;
        current_can_data.fuel_consumption += (float)(CAN_fuel_dose / 1000000.0f);
        CAN_fuel_dose = 0; // reset accumulator
    }

    if (current_can_data.fuel_consumption == last_saved_fuel) {
        return; // No need to save if not changed
    }

    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        uint32_t fuel_u32;
        memcpy(&fuel_u32, &current_can_data.fuel_consumption, sizeof(float)); // Interpret the float bits as uint32_t
        err = nvs_set_u32(my_handle, "fuel_cons", fuel_u32);
        if (err == ESP_OK) {
            err = nvs_commit(my_handle);
            last_saved_fuel = current_can_data.fuel_consumption;
            ESP_LOGI(TAG, "Saved fuel consumption to NVS: %.3f", current_can_data.fuel_consumption);
        }
        nvs_close(my_handle);
    } else {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
    }
}


esp_err_t can_app_init(uint32_t baudrate_kbps) {
    ESP_LOGI(TAG, "Initializing TWAI (CAN) module at %" PRIu32 " kbps", baudrate_kbps);

    last_can_rx_timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);

    // load previously saved fuel consumption value on startup
    can_app_load_fuel_consumption();

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_LISTEN_ONLY); // Changed to LISTEN_ONLY for safety
    twai_timing_config_t t_config;
    
    if (get_timing_config(baudrate_kbps, &t_config) != ESP_OK) {
        return ESP_FAIL;
    }
    
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install TWAI driver");
        return ESP_FAIL;
    }

    if (twai_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start TWAI driver");
        return ESP_FAIL;
    }

    // Reconfigure alerts to detect frame receive and bus errors
    uint32_t alerts_to_enable = TWAI_ALERT_RX_DATA | TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_QUEUE_FULL; 
    if (twai_reconfigure_alerts(alerts_to_enable, NULL) != ESP_OK) {
         ESP_LOGW(TAG, "Failed to reconfigure alerts, proceeding anyway"); 
    }
    
    driver_installed = true;
    ESP_LOGI(TAG, "TWAI (CAN) driver initialized successfully");

    return ESP_OK;
}

void can_app_receive(void) {
    if (!driver_installed) return; 

    uint32_t alerts_triggered = 0; 
    
    // Non-blocking alert check
    twai_read_alerts(&alerts_triggered, 0); 
    
    // Optional error handling for debugging
    if (alerts_triggered & TWAI_ALERT_ERR_PASS) {                                                                              
        ESP_LOGW(TAG, "Alert: TWAI controller has become error passive."); 
    }
    if (alerts_triggered & TWAI_ALERT_BUS_ERROR) {   
        twai_status_info_t twaistatus; 
        twai_get_status_info(&twaistatus);                                                                                               
        ESP_LOGW(TAG, "Alert: Bus error occurred. Count: %" PRIu32, twaistatus.bus_error_count);                
    }

    // If there is incoming data
    if (alerts_triggered & TWAI_ALERT_RX_DATA) { 
        twai_message_t message; 
        // Empties out the entire buffer iteratively
        while (twai_receive(&message, 0) == ESP_OK) {                               
            last_can_rx_timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
            handle_rx_message(message); 
        }
    }
}

uint32_t can_app_get_idle_time_ms(void) {
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    return now_ms - last_can_rx_timestamp_ms;
}

can_data_t* can_app_get_data(void) {
    return &current_can_data;
}

void can_app_reset_fuel_consumptions(void) {
    current_can_data.fuel_consumption = 0;
    CAN_fuel_dose = 0; // reset accumulator
    can_app_save_fuel_consumption(); // Save the reset value to NVS immediately
}