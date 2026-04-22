#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t task_capture_start(uint64_t duration_us, uint32_t frame_interval_ms);
void task_capture_stop(void);
esp_err_t task_capture_wait_done(uint32_t timeout_ms);
