#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "sdmmc_cmd.h"

typedef struct {
    sdmmc_card_t *card;
    bool mounted;
} sdcard_hal_t;

esp_err_t sdcard_hal_init(sdcard_hal_t *out_hal);
void sdcard_hal_deinit(sdcard_hal_t *hal);
bool sdcard_hal_is_ready(const sdcard_hal_t *hal);
