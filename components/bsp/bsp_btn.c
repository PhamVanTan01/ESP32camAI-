#include "bsp_btn.h"

#include <stdint.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "bsp_btn";

static QueueHandle_t s_photo_btn_queue = NULL;
static TaskHandle_t s_photo_btn_task = NULL;
static uint32_t s_last_photo_btn_ms = 0;
static bsp_btn_callback_t s_photo_btn_cb = NULL;

static void IRAM_ATTR photo_btn_isr_handler(void *arg)
{
    if (!s_photo_btn_queue) {
        return;
    }
    uint32_t gpio_num = (uint32_t)(uintptr_t)arg;
    BaseType_t hp_task_woken = pdFALSE;
    xQueueSendFromISR(s_photo_btn_queue, &gpio_num, &hp_task_woken);
    if (hp_task_woken) {
        portYIELD_FROM_ISR();
    }
}

static void photo_btn_task(void *arg)
{
    (void)arg;
    uint32_t gpio_num = 0;

    while (1) {
        if (xQueueReceive(s_photo_btn_queue, &gpio_num, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        if ((now_ms - s_last_photo_btn_ms) < BSP_BTN_DEBOUNCE_MS) {
            continue;
        }
        s_last_photo_btn_ms = now_ms;

        if ((gpio_num_t)gpio_num != BSP_BTN_PHOTO_GPIO) {
            continue;
        }
        if (gpio_get_level(BSP_BTN_PHOTO_GPIO) != BSP_BTN_ACTIVE_LEVEL) {
            continue;
        }

        ESP_LOGI(TAG, "BOOT button pressed");
        if (s_photo_btn_cb) {
            s_photo_btn_cb();
        }
    }
}

esp_err_t bsp_btn_init(bsp_btn_callback_t on_photo_btn_pressed)
{
    s_photo_btn_cb = on_photo_btn_pressed;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BSP_BTN_PHOTO_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    s_photo_btn_queue = xQueueCreate(8, sizeof(uint32_t));
    if (!s_photo_btn_queue) {
        ESP_LOGE(TAG, "Failed to create photo button queue");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_ERROR_CHECK(gpio_isr_handler_add(BSP_BTN_PHOTO_GPIO, photo_btn_isr_handler,
                                         (void *)BSP_BTN_PHOTO_GPIO));

    BaseType_t task_ok = xTaskCreate(photo_btn_task, "photo_btn_task", 4096,
                                     NULL, 5, &s_photo_btn_task);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create photo_btn_task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Photo button ready on GPIO%d (BOOT)", (int)BSP_BTN_PHOTO_GPIO);
    return ESP_OK;
}
