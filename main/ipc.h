#pragma once

#include <stdint.h>
#include "esp_camera.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#define FRAME_QUEUE_DEPTH 4

typedef struct {
    camera_fb_t *fb;
    uint64_t capture_us;
} frame_item_t;

#define EVT_RUNNING (BIT0)
#define EVT_STOP_REQ (BIT1)
#define EVT_WRITER_DONE (BIT2)
#define EVT_CAPTURE_DONE (BIT3)

extern QueueHandle_t g_frame_queue;
extern EventGroupHandle_t g_record_events;

esp_err_t ipc_init(void);
void ipc_deinit(void);
