#include "app_video_auto_test.h"

#include <inttypes.h>
#include "esp_log.h"
#include "app_video.h"

static const char *TAG = "app_video_test";

void app_video_auto_test_run(void)
{
    const app_video_resolution_t *svga = app_video_find_resolution("svga");
    const app_video_resolution_t *qxga = app_video_find_resolution("qxga");
    const app_video_resolution_t *invalid = app_video_find_resolution("bad_resolution");
    const app_video_config_t *cfg = app_video_get_config();
    const char *name = app_video_resolution_name();

    bool ok = true;
    ok = ok && (svga != NULL);
    ok = ok && (qxga != NULL);
    ok = ok && (invalid == NULL);
    ok = ok && (cfg != NULL);
    ok = ok && (name != NULL);

    if (!ok) {
        ESP_LOGE(TAG, "APP VIDEO AUTO TEST FAIL");
        return;
    }

    ESP_LOGI(TAG, "APP VIDEO AUTO TEST PASS: cfg=%dx%d q=%d dur=%" PRIu32 " fps_ms=%" PRIu32 " name=%s",
             cfg->width, cfg->height, cfg->jpeg_quality, cfg->duration_sec, cfg->frame_interval_ms, name);
}
