#include "ipc.h"

QueueHandle_t g_frame_queue = NULL;
EventGroupHandle_t g_record_events = NULL;

esp_err_t ipc_init(void)
{
    if (!g_frame_queue) {
        g_frame_queue = xQueueCreate(FRAME_QUEUE_DEPTH, sizeof(frame_item_t));
        if (!g_frame_queue) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!g_record_events) {
        g_record_events = xEventGroupCreate();
        if (!g_record_events) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

void ipc_deinit(void)
{
    if (g_frame_queue) {
        vQueueDelete(g_frame_queue);
        g_frame_queue = NULL;
    }
    if (g_record_events) {
        vEventGroupDelete(g_record_events);
        g_record_events = NULL;
    }
}
