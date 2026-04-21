#include "boot_autotest.h"

#include <inttypes.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "auto_test.h"

static const char *TAG = "boot_autotest";

#define BOOT_AUTOTEST_ENABLED 1
#define BOOT_AUTOTEST_LOOPS 3
#define BOOT_AUTOTEST_DELAY_MS 1000
#define BOOT_AUTOTEST_START_DELAY_MS 3000

void boot_autotest_start(boot_autotest_action_fn_t photo_action,
                         boot_autotest_action_fn_t record_action)
{
#if BOOT_AUTOTEST_ENABLED
    if (!photo_action || !record_action) {
        ESP_LOGW(TAG, "BOOT AUTOTEST skipped: invalid callbacks");
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(BOOT_AUTOTEST_START_DELAY_MS));
    auto_test_config_t cfg = {
        .photo_action = photo_action,
        .record_action = record_action,
        .loops = BOOT_AUTOTEST_LOOPS,
        .delay_ms = BOOT_AUTOTEST_DELAY_MS,
    };

    esp_err_t ret = auto_test_start_background(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "BOOT AUTOTEST FAIL: start failed (%s)", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "BOOT AUTOTEST started: loops=%" PRIu32 ", delay_ms=%" PRIu32,
             (uint32_t)BOOT_AUTOTEST_LOOPS, (uint32_t)BOOT_AUTOTEST_DELAY_MS);
#else
    (void)photo_action;
    (void)record_action;
#endif
}
