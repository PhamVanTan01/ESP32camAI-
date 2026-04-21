#include "sdcard_smoke_test.h"

#include "esp_log.h"

static const char *TAG = "sdcard_smoke";

void sdcard_smoke_test_run(const sdcard_hal_t *sd_hal)
{
    if (sdcard_hal_is_ready(sd_hal)) {
        ESP_LOGI(TAG, "SDCARD SMOKE TEST PASS");
    } else {
        ESP_LOGE(TAG, "SDCARD SMOKE TEST FAIL");
    }
}
