#include "auto_feature_test.h"

#include <inttypes.h>
#include "esp_log.h"
#include "esp_err.h"
#include "auto_test.h"

static const char *TAG = "auto_feature_test";
static uint32_t s_photo_calls = 0;
static uint32_t s_record_calls = 0;

static esp_err_t photo_probe(void)
{
    s_photo_calls++;
    return ESP_OK;
}

static esp_err_t record_probe(void)
{
    s_record_calls++;
    return ESP_OK;
}

void auto_feature_test_run(void)
{
    s_photo_calls = 0;
    s_record_calls = 0;

    auto_test_config_t cfg = {
        .photo_action = photo_probe,
        .record_action = record_probe,
        .loops = 1,
        .delay_ms = 0,
    };

    auto_test_run_blocking(&cfg);

    if (s_photo_calls == 1 && s_record_calls == 1) {
        ESP_LOGI(TAG, "AUTO FEATURE TEST PASS: photo=%" PRIu32 ", record=%" PRIu32,
                 s_photo_calls, s_record_calls);
    } else {
        ESP_LOGE(TAG, "AUTO FEATURE TEST FAIL: photo=%" PRIu32 ", record=%" PRIu32,
                 s_photo_calls, s_record_calls);
    }
}
