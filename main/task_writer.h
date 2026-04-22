#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef struct {
    const char *video_path;
    int width;
    int height;
    uint32_t init_fps;
} writer_config_t;

esp_err_t task_writer_start(const writer_config_t *cfg);
esp_err_t task_writer_wait_done(uint32_t timeout_ms);
uint32_t task_writer_get_frame_count(void);
uint32_t task_writer_get_write_errors(void);
