#ifndef CAN_H
#define CAN_H

#include <stdint.h>
#include "esp_err.h"

// Structure storing the most recent parsed CAN parameters
typedef struct {
    uint32_t timestamp;
    uint8_t ignition;
    uint8_t speed;
    uint32_t rpm;
    uint8_t throttle_pedal;
    uint16_t brake_pedal;
    uint8_t fuel_level;
    float fuel_consumption; // ml
    uint32_t t_distance;
    uint16_t range;
    int8_t oil_temp;
    uint8_t esp_stat; // bit 0: level 1, bit 1: level 2, bit 2: level 3
    uint8_t rear_gear;
} can_data_t;

#define ESP_ON 0x0
#define ESP_OFF_LEVEL_1 0x02
#define ESP_OFF_LEVEL_2 0x03
#define ESP_MASK 0x03

#define CAN_TX_PIN GPIO_NUM_15 
#define CAN_RX_PIN GPIO_NUM_16 

/**
 * @brief Initializes the TWAI (CAN) interface.
 * 
 * @param baudrate_kbps Speed of the CAN bus in kbps (e.g., 250, 500, 1000).
 * 
 * @return 
 *      - ESP_OK on success
 *      - ESP_FAIL on failure or unsupported baudrate
 */
esp_err_t can_app_init(uint32_t baudrate_kbps);

/**
 * @brief Non-blocking receive function for TWAI (CAN). 
 *        Must be called periodically in the main loop to handle incoming messages.
 */
void can_app_receive(void);

/**
 * @brief Returns time in milliseconds since the last received CAN frame.
 *
 * This timer is updated on every received CAN frame, even for unknown IDs.
 */
uint32_t can_app_get_idle_time_ms(void);

/**
 * @brief Retrieves a pointer to the structure containing the latest CAN data.
 * 
 * @return Pointer to the static can_data_t structure.
 */
can_data_t* can_app_get_data(void);

/**
 * @brief Resets the fuel consumption data.
 */
void can_app_reset_fuel_consumptions(void);

#endif // CAN_H