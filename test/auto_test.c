#include "auto_test.h"

#include <stdlib.h>
#include <inttypes.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "auto_test";

typedef struct {
    auto_test_config_t cfg;
} auto_test_task_ctx_t;

static bool auto_test_cfg_valid(const auto_test_config_t *cfg)
{
    return cfg && cfg->photo_action && cfg->record_action && cfg->loops > 0;
}

void auto_test_run_blocking(const auto_test_config_t *cfg)
{
    if (!auto_test_cfg_valid(cfg)) {
        ESP_LOGE(TAG, "invalid config");
        return;
    }

    uint32_t photo_ok = 0, photo_fail = 0;
    uint32_t record_ok = 0, record_fail = 0;

    ESP_LOGI(TAG, "AUTOTEST start: loops=%" PRIu32 ", delay=%" PRIu32 " ms",
             cfg->loops, cfg->delay_ms);
    for (uint32_t i = 0; i < cfg->loops; i++) {
        ESP_LOGI(TAG, "AUTOTEST loop %" PRIu32 "/%" PRIu32 " -> photo",
                 i + 1, cfg->loops);
        if (cfg->photo_action() == ESP_OK) {
            photo_ok++;
        } else {
            photo_fail++;
        }

        if (cfg->delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(cfg->delay_ms));
        }

        ESP_LOGI(TAG, "AUTOTEST loop %" PRIu32 "/%" PRIu32 " -> record",
                 i + 1, cfg->loops);
        if (cfg->record_action() == ESP_OK) {
            record_ok++;
        } else {
            record_fail++;
        }

        if (cfg->delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(cfg->delay_ms));
        }
    }

    ESP_LOGI(TAG,
             "AUTOTEST done: photo ok=%" PRIu32 " fail=%" PRIu32
             " | record ok=%" PRIu32 " fail=%" PRIu32,
             photo_ok, photo_fail, record_ok, record_fail);
}

static void auto_test_task(void *arg)
{
    auto_test_task_ctx_t *ctx = (auto_test_task_ctx_t *)arg;
    auto_test_run_blocking(&ctx->cfg);
    free(ctx);
    vTaskDelete(NULL);
}

esp_err_t auto_test_start_background(const auto_test_config_t *cfg)
{
    if (!auto_test_cfg_valid(cfg)) {
        return ESP_ERR_INVALID_ARG;
    }

    auto_test_task_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        return ESP_ERR_NO_MEM;
    }
    ctx->cfg = *cfg;

    BaseType_t ok = xTaskCreate(auto_test_task, "auto_test_task", 8192, ctx, 4, NULL);
    if (ok != pdPASS) {
        free(ctx);
        return ESP_FAIL;
    }
    return ESP_OK;
}
