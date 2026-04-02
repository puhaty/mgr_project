# Vehicle Data Display on ESP32-S3

This project is an ESP-IDF application for the Waveshare ESP32-S3 RGB LCD platform. It works as a compact vehicle dashboard: it listens to CAN/TWAI traffic, parses selected vehicle signals, shows them on an LVGL-based touchscreen UI, and stores fuel consumption data in NVS so it survives reboot.

## What it does

- Reads CAN frames in listen-only mode.
- Displays live vehicle values such as speed, RPM, ignition state, throttle, brake, fuel level, fuel consumption, oil temperature, total distance, range, and ESP status.
- Uses an RGB LCD with GT911 touch support through LVGL.
- Mounts an SD card over SPI for filesystem access.
- Saves fuel consumption to NVS when ignition turns off and restores it on startup.
- Enters deep sleep after about 20 seconds without CAN traffic when the UI is not under manual backlight control.

## Hardware

The project is designed for:

- ESP32-S3 based Waveshare RGB LCD hardware.
- GT911 touch controller.
- CAN/TWAI bus wired to GPIO 15 (TX) and GPIO 16 (RX).
- SD card over SPI, with pins controlled by the board support logic in the LCD port layer.

## Requirements

- ESP-IDF 5.1 or newer.
- Python environment configured by ESP-IDF tools.
- A supported ESP32-S3 board with RGB LCD and PSRAM.

## Build

From the project root:

```bash
idf.py set-target esp32s3
idf.py build
```

If you want to adjust board settings before building, run:

```bash
idf.py menuconfig
```

Useful configuration areas include display setup, SD card pins, LVGL timing, and CAN-related hardware behavior.

## Flash and monitor

After a successful build, flash the firmware and open the serial monitor:

```bash
idf.py -p COMX flash monitor
```

Replace `COMX` with your actual serial port, for example `COM5` on Windows.

If you use VS Code with the ESP-IDF extension, the usual flow is:

1. Select the ESP32-S3 target.
2. Build the project.
3. Flash the device.
4. Open monitor output.

## Project structure

- `main/main.c` starts the application, initializes NVS, LCD, SD card, CAN, and the UI loop.
- `main/can.c` handles TWAI initialization, frame parsing, and fuel consumption persistence.
- `main/waveshare_rgb_lcd_port.c` configures the RGB panel, touch controller, and backlight control.
- `main/sd_card.c` mounts the SD card using SPI.
- `main/ui/` contains the generated LVGL UI.

## Notes

- CAN runs in listen-only mode for safety.
- Fuel consumption is stored in NVS under the `storage` namespace.
- The project uses PSRAM and RGB framebuffer settings from the ESP-IDF configuration.
