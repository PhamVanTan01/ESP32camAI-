#include "task_writer.h"

#include <inttypes.h>
#include <string.h>
#include "avi_muxer.h"
#include "camera_hal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ipc.h"

static const char *TAG = "task_writer";
static TaskHandle_t s_writer_task = NULL;
static avi_writer_t *s_writer = NULL;
static uint64_t s_start_us = 0;
static uint32_t s_frame_count = 0;
static uint32_t s_write_errors = 0;

static void writer_task_fn(void *arg)
{
    (void)arg;
    frame_item_t item;
    bool capture_done_seen = false;

    while (true) {
        BaseType_t got = xQueueReceive(g_frame_queue, &item, pdMS_TO_TICKS(200));
        if (got == pdTRUE) {
            if (avi_write_frame(s_writer, item.fb->buf, item.fb->len) != ESP_OK) {
                s_write_errors++;
            } else {
                s_frame_count++;
            }
            camera_hal_return_frame(item.fb);
            continue;
        }

        if (!capture_done_seen) {
            EventBits_t bits = xEventGroupGetBits(g_record_events);
            if (bits & EVT_CAPTURE_DONE) {
                capture_done_seen = true;
                continue;
            }
        } else {
            break;
        }
    }

    uint64_t elapsed_us = esp_timer_get_time() - s_start_us;
    esp_err_t end_ret = avi_end(s_writer, elapsed_us);
    if (end_ret == ESP_OK) {
        float fps = (elapsed_us > 0) ? ((float)s_frame_count * 1e6f / (float)elapsed_us) : 0.0f;
        ESP_LOGI(TAG, "AVI done: %" PRIu32 " frames, %.2f fps, %" PRIu32 " write errors",
                 s_frame_count, fps, s_write_errors);
    } else {
        ESP_LOGE(TAG, "avi_end failed");
    }

    s_writer = NULL;
    s_writer_task = NULL;
    xEventGroupSetBits(g_record_events, EVT_WRITER_DONE);
    vTaskDelete(NULL);
}

esp_err_t task_writer_start(const writer_config_t *cfg)
{
    if (!cfg || !cfg->video_path || s_writer_task) {
        return ESP_ERR_INVALID_ARG;
    }
    s_frame_count = 0;
    s_write_errors = 0;
    s_start_us = esp_timer_get_time();

    esp_err_t ret = avi_begin(cfg->video_path, cfg->width, cfg->height, cfg->init_fps, &s_writer);
    if (ret != ESP_OK) {
        return ret;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        writer_task_fn, "task_writer", 8192, NULL, 5, &s_writer_task, 1);
    if (ok != pdPASS) {
        avi_abort(s_writer);
        s_writer = NULL;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t task_writer_wait_done(uint32_t timeout_ms)
{
    EventBits_t bits = xEventGroupWaitBits(
        g_record_events, EVT_WRITER_DONE, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    return (bits & EVT_WRITER_DONE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

uint32_t task_writer_get_frame_count(void)
{
    return s_frame_count;
}

uint32_t task_writer_get_write_errors(void)
{
    return s_write_errors;
}
