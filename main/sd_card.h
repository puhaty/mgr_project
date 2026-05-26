#ifndef _SD_CARD_
#define _SD_CARD_

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/i2c.h"
#include "waveshare_rgb_lcd_port.h"

// Maximum character size for file operations
#define EXAMPLE_MAX_CHAR_SIZE 64

// Mount point for the SD card
#define MOUNT_POINT "/sdcard"

// Pin assignments for SD SPI interface
#define PIN_NUM_MISO CONFIG_EXAMPLE_PIN_MISO /*!< Pin number for MISO    */
#define PIN_NUM_MOSI CONFIG_EXAMPLE_PIN_MOSI /*!< Pin number for MOSI   */
#define PIN_NUM_CLK CONFIG_EXAMPLE_PIN_CLK   /*!< Pin number for CLK    */
#define PIN_NUM_CS CONFIG_EXAMPLE_PIN_CS     /*!< Pin number for CS CS  */

// Function prototypes for initializing and testing SD card functions
esp_err_t waveshare_sd_card_init();
void sd_card_start_logger_task(void);

// Recording control mode: false = ignition auto mode, true = user/manual mode
void sd_card_set_control_mode(bool user_control_mode);
bool sd_card_is_user_control_mode(void);

// Ignition edge handlers used in ignition auto mode
void sd_card_on_ignition_on(void);
void sd_card_on_ignition_off(void);

esp_err_t sd_card_start_recording(const char *filename);
void sd_card_stop_recording(void);
bool sd_card_is_recording(void);
uint32_t sd_card_get_saved_records(void);

#endif
