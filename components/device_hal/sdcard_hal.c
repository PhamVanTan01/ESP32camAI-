#include "sdcard_hal.h"

#include <string.h>
#include "bsp_board.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"

static const char *TAG = "sdcard_hal";

esp_err_t sdcard_hal_init(sdcard_hal_t *out_hal)
{
    if (!out_hal) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_hal, 0, sizeof(*out_hal));

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    ESP_LOGI(TAG, "Initializing SD card (SDMMC 1-bit, CMD=%d CLK=%d D0=%d)",
             BSP_SD_PIN_CMD, BSP_SD_PIN_CLK, BSP_SD_PIN_D0);

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = BSP_SD_PIN_CLK;
    slot_config.cmd = BSP_SD_PIN_CMD;
    slot_config.d0 = BSP_SD_PIN_D0;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(BSP_SD_MOUNT_POINT, &host, &slot_config,
                                            &mount_config, &out_hal->card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem");
        } else {
            ESP_LOGE(TAG, "Failed to initialize card (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    out_hal->mounted = true;
    ESP_LOGI(TAG, "Filesystem mounted");
    return ESP_OK;
}

void sdcard_hal_deinit(sdcard_hal_t *hal)
{
    if (!hal || !hal->mounted) {
        return;
    }
    esp_vfs_fat_sdcard_unmount(BSP_SD_MOUNT_POINT, hal->card);
    hal->card = NULL;
    hal->mounted = false;
}

bool sdcard_hal_is_ready(const sdcard_hal_t *hal)
{
    return hal && hal->mounted && hal->card;
}
