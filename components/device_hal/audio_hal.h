#pragma once

#include <stdio.h>
#include "esp_err.h"

esp_err_t audio_hal_init(void);
void audio_hal_deinit(void);
esp_err_t audio_hal_record_chunk(FILE *audio_file);
