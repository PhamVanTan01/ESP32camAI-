#include "task_capture.h"

#include <inttypes.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "camera_hal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ipc.h"

static const char *TAG = "task_capture";
static TaskHandle_t s_capture_task = NULL;
static uint64_t s_duration_us = 0;
static uint32_t s_frame_interval_ms = 0;

static bool should_stop(uint64_t start_us)
{
    EventBits_t bits = xEventGroupGetBits(g_record_events);
    if (bits & EVT_STOP_REQ) {
        return true;
    }
    return (esp_timer_get_time() - start_us) >= s_duration_us;
}

static void capture_task_fn(void *arg)
{
    (void)arg;
    uint64_t start_us = esp_timer_get_time();
    uint32_t drop_count = 0;
    uint32_t fail_streak = 0;

    xEventGroupSetBits(g_record_events, EVT_RUNNING);

    while (!should_stop(start_us)) {
        uint64_t frame_start = esp_timer_get_time();
        camera_fb_t *fb = camera_hal_get_frame();
        if (!fb) {
            fail_streak++;
            if (fail_streak >= 50) {
                ESP_LOGE(TAG, "Abort: %" PRIu32 " consecutive capture failures", fail_streak);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        fail_streak = 0;
        if (fb->format != PIXFORMAT_JPEG) {
            camera_hal_return_frame(fb);
            continue;
        }

        frame_item_t item = {
            .fb = fb,
            .capture_us = esp_timer_get_time(),
        };
        if (xQueueSend(g_frame_queue, &item, 0) != pdTRUE) {
            camera_hal_return_frame(fb);
            drop_count++;
            if ((drop_count % 10) == 1) {
                ESP_LOGW(TAG, "Frame dropped (queue full), total=%" PRIu32, drop_count);
            }
        }

        if (s_frame_interval_ms > 0) {
            uint64_t elapsed_ms = (esp_timer_get_time() - frame_start) / 1000ULL;
            if (elapsed_ms < s_frame_interval_ms) {
                vTaskDelay(pdMS_TO_TICKS(s_frame_interval_ms - elapsed_ms));
            }
        }
    }

    xEventGroupSetBits(g_record_events, EVT_CAPTURE_DONE);
    s_capture_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t task_capture_start(uint64_t duration_us, uint32_t frame_interval_ms)
{
    if (s_capture_task) {
        return ESP_ERR_INVALID_STATE;
    }
    s_duration_us = duration_us;
    s_frame_interval_ms = frame_interval_ms;
    BaseType_t ok = xTaskCreatePinnedToCore(
        capture_task_fn, "task_capture", 4096, NULL, 6, &s_capture_task, 0);
    return (ok == pdPASS) ? ESP_OK : ESP_FAIL;
}

void task_capture_stop(void)
{
    xEventGroupSetBits(g_record_events, EVT_STOP_REQ);
}

esp_err_t task_capture_wait_done(uint32_t timeout_ms)
{
    EventBits_t bits = xEventGroupWaitBits(
        g_record_events, EVT_CAPTURE_DONE, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    return (bits & EVT_CAPTURE_DONE) ? ESP_OK : ESP_ERR_TIMEOUT;
}
