#include "smoke_test.h"

#include <string.h>
#include "esp_log.h"
#include "bsp_board.h"
#include "file_transfer.h"

static const char *TAG = "smoke_test";

static bool str_eq(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

void smoke_test_run(void)
{
    char out[64];
    bool ok = true;

    file_transfer_build_vfs_path(out, sizeof(out), BSP_SD_MOUNT_POINT, "demo.bin");
    if (!str_eq(out, BSP_SD_MOUNT_POINT "/demo.bin")) {
        ESP_LOGE(TAG, "build_vfs_path relative failed: got=%s", out);
        ok = false;
    }

    file_transfer_build_vfs_path(out, sizeof(out), BSP_SD_MOUNT_POINT, "/tmp/raw.bin");
    if (!str_eq(out, "/tmp/raw.bin")) {
        ESP_LOGE(TAG, "build_vfs_path absolute failed: got=%s", out);
        ok = false;
    }

    if (ok) {
        ESP_LOGI(TAG, "SMOKE TEST PASS");
    } else {
        ESP_LOGE(TAG, "SMOKE TEST FAIL");
    }
}
