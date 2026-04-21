#pragma once

#include "esp_err.h"
#include "esp_camera.h"

typedef struct {
    framesize_t framesize;
    int jpeg_quality;
    int fb_count;
} camera_hal_config_t;

esp_err_t camera_hal_init(void);
void camera_hal_deinit(void);
esp_err_t camera_hal_apply_config(const camera_hal_config_t *cfg);
esp_err_t camera_hal_set_quality(int quality);
camera_fb_t *camera_hal_get_frame(void);
void camera_hal_return_frame(camera_fb_t *fb);
sensor_t *camera_hal_get_sensor(void);
