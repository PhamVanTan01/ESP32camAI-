#pragma once

#include "driver/gpio.h"

// ═══════════════════════════════════════════════════════
//  Camera DVP pins (GOOUUU ESP32-S3-CAM)
// ═══════════════════════════════════════════════════════
#define BSP_CAM_PIN_PWDN    -1
#define BSP_CAM_PIN_RESET   -1
#define BSP_CAM_PIN_XCLK    15
#define BSP_CAM_PIN_SIOD     4
#define BSP_CAM_PIN_SIOC     5
#define BSP_CAM_PIN_D7      16
#define BSP_CAM_PIN_D6      17
#define BSP_CAM_PIN_D5      18
#define BSP_CAM_PIN_D4      12
#define BSP_CAM_PIN_D3      10
#define BSP_CAM_PIN_D2       8
#define BSP_CAM_PIN_D1       9
#define BSP_CAM_PIN_D0      11
#define BSP_CAM_PIN_VSYNC    6
#define BSP_CAM_PIN_HREF     7
#define BSP_CAM_PIN_PCLK    13

// ═══════════════════════════════════════════════════════
//  SD Card — SDMMC 1-bit (onboard)
// ═══════════════════════════════════════════════════════
#define BSP_SD_PIN_CMD      38
#define BSP_SD_PIN_CLK      39
#define BSP_SD_PIN_D0       40
#define BSP_SD_MOUNT_POINT  "/sdcard"

// ═══════════════════════════════════════════════════════
//  Button
// ═══════════════════════════════════════════════════════
#define BSP_BTN_PHOTO_GPIO   GPIO_NUM_0
#define BSP_BTN_ACTIVE_LEVEL 0
#define BSP_BTN_DEBOUNCE_MS  250

// ═══════════════════════════════════════════════════════
//  Audio — PDM Microphone (optional external)
// ═══════════════════════════════════════════════════════
#define BOARD_HAS_PDM_MIC    0
#if BOARD_HAS_PDM_MIC
#include "driver/i2s_std.h"
#define BSP_I2S_CLK_IO       41
#define BSP_I2S_DIN_IO       42
#define BSP_I2S_PORT         I2S_NUM_0
#define BSP_I2S_SAMPLE_RATE  16000
#define BSP_I2S_CHANNELS     1
#define BSP_I2S_BITS         I2S_DATA_BIT_WIDTH_16BIT
#endif

// ═══════════════════════════════════════════════════════
//  Board capabilities flags
// ═══════════════════════════════════════════════════════
#define BOARD_HAS_PSRAM      1
#define BOARD_HAS_CAMERA     1
#define BOARD_HAS_SDCARD     1

/** Print chip / flash / PSRAM / DRAM summary to stdout (UART console). */
void bsp_board_print_info(void);
