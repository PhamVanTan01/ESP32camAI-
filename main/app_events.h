#pragma once

#include "esp_err.h"
#include "esp_event.h"

ESP_EVENT_DECLARE_BASE(APP_EVENTS);

typedef enum {
    APP_EVENT_BTN_PHOTO = 0,
    APP_EVENT_RECORD_START,
    APP_EVENT_RECORD_STOP,
} app_event_id_t;

esp_err_t app_events_init(void);
void app_events_post(app_event_id_t event_id);
esp_err_t app_events_register(app_event_id_t event_id, esp_event_handler_t handler);
