#include "auto_file_test.h"

#include <stdio.h>
#include <inttypes.h>
#include <time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "bsp_board.h"

static const char *TAG = "auto_file_test";

void auto_file_test_run(const sdcard_hal_t *sd_hal)
{
    if (!sdcard_hal_is_ready(sd_hal)) {
        ESP_LOGW(TAG, "Skip auto file test: SD card not ready");
        return;
    }

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    uint32_t ms = (uint32_t)((esp_timer_get_time() / 1000ULL) % 1000ULL);

    char path[96];
    snprintf(path, sizeof(path), BSP_SD_MOUNT_POINT "/auto_test_%02d%02d%02d_%03lu.txt",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, (unsigned long)ms);

    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "AUTO FILE TEST FAIL: fopen(%s) failed", path);
        return;
    }

    int n = fprintf(f, "auto file test ok\n");
    fclose(f);
    if (n <= 0) {
        ESP_LOGE(TAG, "AUTO FILE TEST FAIL: write failed (%s)", path);
        return;
    }

    ESP_LOGI(TAG, "AUTO FILE TEST PASS: %s", path);
}
