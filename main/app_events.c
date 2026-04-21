#include "app_events.h"

#include "esp_log.h"

ESP_EVENT_DEFINE_BASE(APP_EVENTS);

static const char *TAG = "app_events";
static bool s_inited = false;

esp_err_t app_events_init(void)
{
    esp_err_t ret = esp_event_loop_create_default();
    if (ret == ESP_ERR_INVALID_STATE) {
        s_inited = true;
        return ESP_OK;
    }
    if (ret == ESP_OK) {
        s_inited = true;
    }
    return ret;
}

void app_events_post(app_event_id_t event_id)
{
    if (!s_inited) {
        ESP_LOGW(TAG, "post ignored: app_events not initialized");
        return;
    }
    esp_err_t ret = esp_event_post(APP_EVENTS, (int32_t)event_id, NULL, 0, portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_event_post failed: %s", esp_err_to_name(ret));
    }
}

esp_err_t app_events_register(app_event_id_t event_id, esp_event_handler_t handler)
{
    if (!handler) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_event_handler_register(APP_EVENTS, (int32_t)event_id, handler, NULL);
}
