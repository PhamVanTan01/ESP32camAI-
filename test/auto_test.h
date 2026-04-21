#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef esp_err_t (*auto_test_action_fn_t)(void);

typedef struct {
    auto_test_action_fn_t photo_action;
    auto_test_action_fn_t record_action;
    uint32_t loops;
    uint32_t delay_ms;
} auto_test_config_t;

void auto_test_run_blocking(const auto_test_config_t *cfg);
esp_err_t auto_test_start_background(const auto_test_config_t *cfg);
