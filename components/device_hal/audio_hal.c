#include "audio_hal.h"

#include "bsp_board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "driver/i2s_std.h"
#include "driver/i2s_pdm.h"

#if BOARD_HAS_PDM_MIC
#define DMA_BUFFER_LEN 1024
#define AUDIO_BUFFER_SIZE (DMA_BUFFER_LEN * 2)

static const char *TAG = "audio_hal";
static i2s_chan_handle_t s_i2s_handle = NULL;
static uint8_t s_audio_buffer[AUDIO_BUFFER_SIZE];

esp_err_t audio_hal_init(void)
{
    ESP_LOGI(TAG, "Initializing I2S");

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BSP_I2S_PORT, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &s_i2s_handle), TAG, "i2s_new_channel failed");

    i2s_pdm_rx_config_t pdm_rx_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(BSP_I2S_SAMPLE_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(BSP_I2S_BITS, BSP_I2S_CHANNELS),
        .gpio_cfg = {
            .clk = BSP_I2S_CLK_IO,
            .din = BSP_I2S_DIN_IO,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };

    esp_err_t ret = i2s_channel_init_pdm_rx_mode(s_i2s_handle, &pdm_rx_cfg);
    if (ret != ESP_OK) {
        i2s_del_channel(s_i2s_handle);
        s_i2s_handle = NULL;
        return ret;
    }

    ret = i2s_channel_enable(s_i2s_handle);
    if (ret != ESP_OK) {
        i2s_del_channel(s_i2s_handle);
        s_i2s_handle = NULL;
    }
    return ret;
}

void audio_hal_deinit(void)
{
    if (s_i2s_handle) {
        i2s_channel_disable(s_i2s_handle);
        i2s_del_channel(s_i2s_handle);
        s_i2s_handle = NULL;
    }
}

esp_err_t audio_hal_record_chunk(FILE *audio_file)
{
    if (!audio_file || !s_i2s_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t bytes_read = 0;
    esp_err_t ret = i2s_channel_read(s_i2s_handle, s_audio_buffer, AUDIO_BUFFER_SIZE, &bytes_read, portMAX_DELAY);
    if (ret != ESP_OK) {
        return ret;
    }

    size_t bytes_written = fwrite(s_audio_buffer, 1, bytes_read, audio_file);
    return (bytes_written == bytes_read) ? ESP_OK : ESP_FAIL;
}

#else
esp_err_t audio_hal_init(void)
{
    return ESP_OK;
}

void audio_hal_deinit(void)
{
}

esp_err_t audio_hal_record_chunk(FILE *audio_file)
{
    (void)audio_file;
    return ESP_ERR_NOT_SUPPORTED;
}
#endif
