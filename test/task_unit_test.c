#include "task_unit_test.h"

#include "esp_log.h"
#include "app_cli.h"
#include "app_video.h"

static const char *TAG = "task_unit_test";

void task_unit_test_run(void)
{
    bool ok = true;

    if (app_cli_start() != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Expected app_cli_start() to fail before init");
        ok = false;
    }

    if (app_video_record() != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Expected app_video_record() to fail before bind");
        ok = false;
    }

    if (app_video_capture_photo() != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Expected app_video_capture_photo() to fail before bind");
        ok = false;
    }

    if (app_video_find_resolution("svga") == NULL ||
        app_video_find_resolution("qxga") == NULL ||
        app_video_find_resolution("not_exists") != NULL) {
        ESP_LOGE(TAG, "Resolution lookup contract failed");
        ok = false;
    }

    if (ok) {
        ESP_LOGI(TAG, "TASK UNIT TEST PASS");
    } else {
        ESP_LOGE(TAG, "TASK UNIT TEST FAIL");
    }
}
