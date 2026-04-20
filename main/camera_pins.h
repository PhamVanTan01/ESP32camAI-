#pragma once

// GOOUUU ESP32-S3-CAM / ESP32-S3-WROOM (CAM) — DVP same as ESP32-S3-EYE

#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    15
#define SIOD_GPIO_NUM    4
#define SIOC_GPIO_NUM    5

#define Y9_GPIO_NUM      16
#define Y8_GPIO_NUM      17
#define Y7_GPIO_NUM      18
#define Y6_GPIO_NUM      12
#define Y5_GPIO_NUM      10
#define Y4_GPIO_NUM      8
#define Y3_GPIO_NUM      9
#define Y2_GPIO_NUM      11
#define VSYNC_GPIO_NUM   6
#define HREF_GPIO_NUM    7
#define PCLK_GPIO_NUM    13

// Onboard TF: SDMMC 1-bit (CMD / CLK / DAT0)
#define SD_PIN_CMD       38
#define SD_PIN_CLK       39
#define SD_PIN_D0        40

// No onboard PDM mic on this module. Set to 1 if you wire an external PDM mic.
#define BOARD_HAS_PDM_MIC 0

#if BOARD_HAS_PDM_MIC
#define I2S_CLK_IO       41
#define I2S_DIN_IO       42
#define I2S_PORT         I2S_NUM_0
#define I2S_SAMPLE_RATE  16000
#define I2S_CHANNEL_NUM  1
#define I2S_DATA_BIT_WIDTH I2S_DATA_BIT_WIDTH_16BIT
#endif

#define MOUNT_POINT "/sdcard"
