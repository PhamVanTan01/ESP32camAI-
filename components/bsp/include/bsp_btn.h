#pragma once

#include "esp_err.h"
#include "bsp_board.h"

typedef void (*bsp_btn_callback_t)(void);

/** Initialize BOOT photo button and register callback called from button task context. */
esp_err_t bsp_btn_init(bsp_btn_callback_t on_photo_btn_pressed);
