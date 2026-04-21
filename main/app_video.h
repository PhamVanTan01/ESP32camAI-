#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_camera.h"
#include "esp_err.h"
#include "sdcard_hal.h"

typedef struct {
    framesize_t framesize;
    int width;
    int height;
    int jpeg_quality;
    uint32_t duration_sec;
    uint32_t frame_interval_ms;
} app_video_config_t;

typedef struct {
    const char *name;
    framesize_t fs;
    int width;
    int height;
} app_video_resolution_t;

esp_err_t app_video_init(void);
void app_video_set_sdcard(sdcard_hal_t *sdcard);
const app_video_config_t *app_video_get_config(void);
app_video_config_t *app_video_get_config_mutable(void);
const app_video_resolution_t *app_video_find_resolution(const char *name);
const char *app_video_resolution_name(void);
esp_err_t app_video_apply_config(void);
esp_err_t app_video_apply_quality(int quality);
esp_err_t app_video_record(void);
esp_err_t app_video_capture_photo(void);
