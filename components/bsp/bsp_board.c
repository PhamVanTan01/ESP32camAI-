#include <inttypes.h>
#include <stdio.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "sdkconfig.h"

#include "bsp_board.h"

static const char *chip_model_str(esp_chip_model_t model)
{
    switch (model) {
    case CHIP_ESP32:         return "ESP32";
    case CHIP_ESP32S2:       return "ESP32-S2";
    case CHIP_ESP32S3:       return "ESP32-S3";
    case CHIP_ESP32C3:       return "ESP32-C3";
    case CHIP_ESP32C2:       return "ESP32-C2";
    case CHIP_ESP32C6:       return "ESP32-C6";
    case CHIP_ESP32H2:       return "ESP32-H2";
    case CHIP_ESP32P4:       return "ESP32-P4";
    case CHIP_POSIX_LINUX:   return "POSIX/Linux";
    default:                 return "unknown";
    }
}

void bsp_board_print_info(void)
{
    esp_chip_info_t info;
    esp_chip_info(&info);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psram_free  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t dram_total  = heap_caps_get_total_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    size_t dram_free   = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

    printf("\n================ CHIP INFO ================\n");
    printf("Chip:        %s rev v%d.%d, %d core(s)\n",
           chip_model_str(info.model),
           info.revision / 100, info.revision % 100,
           info.cores);
    printf("Features:    %s%s%s%s\n",
           (info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (info.features & CHIP_FEATURE_BT)       ? "BT/"   : "",
           (info.features & CHIP_FEATURE_BLE)      ? "BLE/"  : "",
           (info.features & CHIP_FEATURE_IEEE802154) ? "802.15.4" : "");
    printf("CPU freq:    %d MHz\n", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    printf("IDF ver:     %s\n", esp_get_idf_version());
    printf("-------------------------------------------\n");
    printf("Flash:       %" PRIu32 " MB (%" PRIu32 " bytes) %s\n",
           flash_size / (1024 * 1024), flash_size,
           (info.features & CHIP_FEATURE_EMB_FLASH) ? "(embedded)" : "(external)");
    if (psram_total > 0) {
        printf("PSRAM:       %u KB total, %u KB free\n",
               (unsigned)(psram_total / 1024), (unsigned)(psram_free / 1024));
    } else {
        printf("PSRAM:       not enabled (CONFIG_SPIRAM=n) or not detected\n");
    }
    printf("DRAM:        %u KB total, %u KB free\n",
           (unsigned)(dram_total / 1024), (unsigned)(dram_free / 1024));
    printf("Largest free internal block: %u KB\n",
           (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL) / 1024));
    printf("===========================================\n\n");
}
