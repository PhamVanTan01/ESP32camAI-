#include "app_events_auto_test.h"

#include "app_events.h"
#include "esp_log.h"

static const char *TAG = "app_events_test";
static volatile int s_test_hits = 0;

static void on_record_start_test(void *handler_arg, esp_event_base_t base,
                                 int32_t id, void *event_data)
{
    (void)handler_arg;
    (void)base;
    (void)id;
    (void)event_data;
    s_test_hits++;
}

void app_events_auto_test_run(void)
{
    s_test_hits = 0;

    esp_err_t reg_ret = app_events_register(APP_EVENT_RECORD_START, on_record_start_test);
    if (reg_ret != ESP_OK) {
        ESP_LOGE(TAG, "APP EVENTS AUTO TEST FAIL: register failed (%s)", esp_err_to_name(reg_ret));
        return;
    }

    app_events_post(APP_EVENT_RECORD_START);

    if (s_test_hits > 0) {
        ESP_LOGI(TAG, "APP EVENTS AUTO TEST PASS");
    } else {
        ESP_LOGE(TAG, "APP EVENTS AUTO TEST FAIL: handler not triggered");
    }
}
