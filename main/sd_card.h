#ifndef _SD_CARD_
#define _SD_CARD_

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

#endif
