#include "task_smoke_test.h"

#include <string.h>
#include "esp_log.h"
#include "app_cli.h"
#include "app_video.h"

static const char *TAG = "task_smoke_test";

void task_smoke_test_run(void)
{
    bool ok = true;
    const app_video_config_t *cfg = app_video_get_config();
    const char *name = app_video_resolution_name();

    if (!cfg || !name || strlen(name) == 0) {
        ESP_LOGE(TAG, "Invalid video config/name");
        ok = false;
    }

    if (app_cli_init() != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Expected second app_cli_init() to return INVALID_STATE");
        ok = false;
    }

    if (ok) {
        ESP_LOGI(TAG, "TASK SMOKE TEST PASS");
    } else {
        ESP_LOGE(TAG, "TASK SMOKE TEST FAIL");
    }
}
