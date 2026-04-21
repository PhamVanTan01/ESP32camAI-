#include "camera_hal.h"

#include "bsp_board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "camera_hal";
static bool s_camera_inited = false;

esp_err_t camera_hal_init(void)
{
    camera_config_t cfg = {
        .pin_pwdn = BSP_CAM_PIN_PWDN,
        .pin_reset = BSP_CAM_PIN_RESET,
        .pin_xclk = BSP_CAM_PIN_XCLK,
        .pin_sccb_sda = BSP_CAM_PIN_SIOD,
        .pin_sccb_scl = BSP_CAM_PIN_SIOC,
        .pin_d7 = BSP_CAM_PIN_D7,
        .pin_d6 = BSP_CAM_PIN_D6,
        .pin_d5 = BSP_CAM_PIN_D5,
        .pin_d4 = BSP_CAM_PIN_D4,
        .pin_d3 = BSP_CAM_PIN_D3,
        .pin_d2 = BSP_CAM_PIN_D2,
        .pin_d1 = BSP_CAM_PIN_D1,
        .pin_d0 = BSP_CAM_PIN_D0,
        .pin_vsync = BSP_CAM_PIN_VSYNC,
        .pin_href = BSP_CAM_PIN_HREF,
        .pin_pclk = BSP_CAM_PIN_PCLK,
        .xclk_freq_hz = 16000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_SVGA,
        .jpeg_quality = 12,
        .fb_count = 2,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST,
    };

    esp_err_t ret = esp_camera_init(&cfg);
    if (ret == ESP_OK) {
        s_camera_inited = true;
    }
    return ret;
}

void camera_hal_deinit(void)
{
    if (s_camera_inited) {
        esp_camera_deinit();
        s_camera_inited = false;
    }
}

esp_err_t camera_hal_apply_config(const camera_hal_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    sensor_t *s = camera_hal_get_sensor();
    if (!s) {
        ESP_LOGE(TAG, "camera sensor is not ready");
        return ESP_FAIL;
    }
    if (s->set_framesize(s, cfg->framesize) != 0) {
        ESP_LOGE(TAG, "set_framesize(%d) failed", (int)cfg->framesize);
        return ESP_FAIL;
    }
    if (s->set_quality(s, cfg->jpeg_quality) != 0) {
        ESP_LOGE(TAG, "set_quality(%d) failed", cfg->jpeg_quality);
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(1500));
    return ESP_OK;
}

esp_err_t camera_hal_set_quality(int quality)
{
    sensor_t *s = camera_hal_get_sensor();
    if (!s) {
        return ESP_FAIL;
    }
    return (s->set_quality(s, quality) == 0) ? ESP_OK : ESP_FAIL;
}

camera_fb_t *camera_hal_get_frame(void)
{
    return esp_camera_fb_get();
}

void camera_hal_return_frame(camera_fb_t *fb)
{
    if (fb) {
        esp_camera_fb_return(fb);
    }
}

sensor_t *camera_hal_get_sensor(void)
{
    if (!s_camera_inited) {
        return NULL;
    }
    return esp_camera_sensor_get();
}
