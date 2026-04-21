#include "nvs_flash.h"
#include "esp_log.h"
#include "bsp_board.h"
#include "bsp_btn.h"
#include "app_events.h"
#include "app_video.h"
#include "app_cli.h"
#include "sdcard_hal.h"
#include "audio_hal.h"
#include "camera_hal.h"
#if BOARD_HAS_PDM_MIC
#endif

static const char *TAG = "video_recorder";

static sdcard_hal_t s_sdcard = {0};

static void on_btn_photo_event(void *handler_arg, esp_event_base_t base,
                               int32_t id, void *event_data)
{
    (void)handler_arg;
    (void)base;
    (void)id;
    (void)event_data;
    if (app_video_capture_photo() != ESP_OK) {
        ESP_LOGW(TAG, "Photo capture failed from APP_EVENT_BTN_PHOTO");
    }
}

static void on_photo_button_pressed(void)
{
    app_events_post(APP_EVENT_BTN_PHOTO);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(app_video_init());
    app_video_set_sdcard(&s_sdcard);

    ESP_ERROR_CHECK(camera_hal_init());
    const app_video_config_t *cfg = app_video_get_config();
    ESP_LOGI(TAG, "Camera initialized: %s %dx%d q=%d",
             app_video_resolution_name(), cfg->width, cfg->height,
             cfg->jpeg_quality);

#if BOARD_HAS_PDM_MIC
    ESP_ERROR_CHECK(audio_hal_init());
    ESP_LOGI(TAG, "I2S initialized");
#endif

    esp_err_t sd_ret = sdcard_hal_init(&s_sdcard);
    if (sd_ret == ESP_OK) {
        ESP_LOGI(TAG, "SD card initialized");
    } else {
        ESP_LOGW(TAG, "SD card not available (%s) — REPL will still start, "
                 "but `record`/`ls`/`transfer` will fail. "
                 "Insert a FAT32 microSD card and reboot.",
                 esp_err_to_name(sd_ret));
    }

    // Print chip info AFTER all hardware init so the long printf output
    // doesn't push SD-card init timing around on boot.
    bsp_board_print_info();

    ESP_ERROR_CHECK(app_events_init());
    ESP_ERROR_CHECK(app_events_register(APP_EVENT_BTN_PHOTO, on_btn_photo_event));

    esp_err_t btn_ret = bsp_btn_init(&on_photo_button_pressed);
    if (btn_ret != ESP_OK) {
        ESP_LOGW(TAG, "Photo button init failed (%s); command `photo` still works",
                 esp_err_to_name(btn_ret));
    }

    ESP_ERROR_CHECK(app_cli_init());

    ESP_ERROR_CHECK(app_cli_start());

#if BOARD_HAS_PDM_MIC
    atexit(audio_hal_deinit);
#endif
}
