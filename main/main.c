#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "nvs_flash.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_idf_version.h"
#include "esp_psram.h"
#include "camera_pins.h"
#if BOARD_HAS_PDM_MIC
#include "driver/i2s_std.h"
#include "driver/i2s_pdm.h"
#endif

static const char *TAG = "video_recorder";

#define PHOTO_BTN_GPIO          GPIO_NUM_0
#define PHOTO_BTN_ACTIVE_LEVEL  0
#define PHOTO_BTN_DEBOUNCE_MS   250

#if BOARD_HAS_PDM_MIC
#define DMA_BUFFER_LEN      1024
#define AUDIO_BUFFER_SIZE (DMA_BUFFER_LEN * 2)
#endif

// Camera configuration
static camera_config_t camera_config = {
    .pin_pwdn = PWDN_GPIO_NUM,
    .pin_reset = RESET_GPIO_NUM,
    .pin_xclk = XCLK_GPIO_NUM,
    .pin_sccb_sda = SIOD_GPIO_NUM,
    .pin_sccb_scl = SIOC_GPIO_NUM,

    .pin_d7 = Y9_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM,
    .pin_d5 = Y7_GPIO_NUM,
    .pin_d4 = Y6_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM,
    .pin_d2 = Y4_GPIO_NUM,
    .pin_d1 = Y3_GPIO_NUM,
    .pin_d0 = Y2_GPIO_NUM,

    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href = HREF_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM,

    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG,
    // Init at the default runtime resolution (SVGA). Frame buffer size is
    // pinned to 2 MB by CONFIG_CAMERA_JPEG_MODE_FRAME_SIZE (sdkconfig.defaults).
    // fb_count=2 + GRAB_LATEST: two rotating buffers in PSRAM so the driver
    // always has a fresh frame when the app calls esp_camera_fb_get(). This
    // also matches the baseline config that was proven stable earlier in the
    // project.
    .frame_size = FRAMESIZE_SVGA,
    .jpeg_quality = 12,
    .fb_count = 2,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_LATEST
};

// ---------------------------------------------------------------------------
// Runtime video configuration (mutable via `set` / queried via `show`).
// ---------------------------------------------------------------------------

typedef struct {
    framesize_t framesize;
    int         width;
    int         height;
    int         jpeg_quality;         // 0..63, lower = better
    uint32_t    duration_sec;          // record length in seconds
    uint32_t    frame_interval_ms;     // 0 = no cap, else throttle capture loop
} video_config_t;

static video_config_t g_video = {
    .framesize          = FRAMESIZE_SVGA,
    .width              = 800,
    .height             = 600,
    .jpeg_quality       = 12,
    .duration_sec       = 30,
    .frame_interval_ms  = 0,
};

typedef struct {
    const char *name;
    framesize_t fs;
    int         width;
    int         height;
} resolution_entry_t;

static const resolution_entry_t g_resolutions[] = {
    {"qqvga", FRAMESIZE_QQVGA,  160,  120},
    {"qvga",  FRAMESIZE_QVGA,   320,  240},
    {"cif",   FRAMESIZE_CIF,    400,  296},
    {"hvga",  FRAMESIZE_HVGA,   480,  320},
    {"vga",   FRAMESIZE_VGA,    640,  480},
    {"svga",  FRAMESIZE_SVGA,   800,  600},
    {"xga",   FRAMESIZE_XGA,   1024,  768},
    {"hd",    FRAMESIZE_HD,    1280,  720},
    {"sxga",  FRAMESIZE_SXGA,  1280, 1024},
    {"uxga",  FRAMESIZE_UXGA,  1600, 1200},
    {"fhd",   FRAMESIZE_FHD,   1920, 1080},
    {"qxga",  FRAMESIZE_QXGA,  2048, 1536},
};
static const size_t g_resolutions_count =
    sizeof(g_resolutions) / sizeof(g_resolutions[0]);

static const resolution_entry_t *find_resolution(const char *name)
{
    for (size_t i = 0; i < g_resolutions_count; i++) {
        if (strcasecmp(g_resolutions[i].name, name) == 0) {
            return &g_resolutions[i];
        }
    }
    return NULL;
}

static const char *current_resolution_name(void)
{
    for (size_t i = 0; i < g_resolutions_count; i++) {
        if (g_resolutions[i].fs == g_video.framesize) {
            return g_resolutions[i].name;
        }
    }
    return "?";
}

// Apply g_video settings via sensor_t register writes (NO camera deinit/init).
// We avoid esp_camera_deinit()+esp_camera_init() because each cycle frees
// and re-requests a 2 MB PSRAM block -> PSRAM heap fragments after a few
// changes and the second fb_count slot fails to allocate.
//
// Trade-off: OV5640 takes ~1-1.5 s to fully settle its PLL + JPEG pipeline
// after a framesize change. During that window frames come out as NO-EOI
// and get dropped by cam_hal. We sleep long enough for the sensor to
// recover before returning.
static esp_err_t apply_video_config(void)
{
    sensor_t *s = esp_camera_sensor_get();
    if (!s) {
        ESP_LOGE(TAG, "esp_camera_sensor_get() returned NULL");
        return ESP_FAIL;
    }
    if (s->set_framesize(s, g_video.framesize) != 0) {
        ESP_LOGE(TAG, "set_framesize(%d) failed", (int)g_video.framesize);
        return ESP_FAIL;
    }
    if (s->set_quality(s, g_video.jpeg_quality) != 0) {
        ESP_LOGE(TAG, "set_quality(%d) failed", g_video.jpeg_quality);
        return ESP_FAIL;
    }
    // Let sensor PLL re-lock and AE/AWB settle before the next record.
    vTaskDelay(pdMS_TO_TICKS(1500));
    return ESP_OK;
}

// Lightweight path used when only jpeg_quality changes (no framesize change).
static esp_err_t apply_quality_only(int q)
{
    sensor_t *s = esp_camera_sensor_get();
    if (!s) {
        return ESP_FAIL;
    }
    if (s->set_quality(s, q) != 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

#if BOARD_HAS_PDM_MIC
static i2s_chan_handle_t i2s_handle = NULL;
static uint8_t audio_buffer[AUDIO_BUFFER_SIZE];
#endif

static sdmmc_card_t *s_sd_card = NULL;
static QueueHandle_t s_photo_btn_queue = NULL;
static TaskHandle_t s_photo_btn_task = NULL;
static uint32_t s_last_photo_btn_ms = 0;

static esp_err_t init_sdcard(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    ESP_LOGI(TAG, "Initializing SD card (SDMMC 1-bit, CMD=%d CLK=%d D0=%d)",
             SD_PIN_CMD, SD_PIN_CLK, SD_PIN_D0);

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = SD_PIN_CLK;
    slot_config.cmd = SD_PIN_CMD;
    slot_config.d0 = SD_PIN_D0;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &s_sd_card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                     "Ensure 10k pull-ups on CMD/D0 and a FAT32 card.", esp_err_to_name(ret));
        }
        return ret;
    }

    ESP_LOGI(TAG, "Filesystem mounted");
    return ESP_OK;
}

#if BOARD_HAS_PDM_MIC
static esp_err_t init_i2s(void)
{
    ESP_LOGI(TAG, "Initializing I2S");

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &i2s_handle));

    i2s_pdm_rx_config_t pdm_rx_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH, I2S_CHANNEL_NUM),
        .gpio_cfg = {
            .clk = I2S_CLK_IO,
            .din = I2S_DIN_IO,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_pdm_rx_mode(i2s_handle, &pdm_rx_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_handle));

    return ESP_OK;
}

static void deinit_i2s(void)
{
    if (i2s_handle) {
        i2s_channel_disable(i2s_handle);
        i2s_del_channel(i2s_handle);
        i2s_handle = NULL;
    }
}

static esp_err_t record_audio_chunk(FILE *audio_file)
{
    size_t bytes_read = 0;
    esp_err_t ret = i2s_channel_read(i2s_handle, audio_buffer, AUDIO_BUFFER_SIZE, &bytes_read, portMAX_DELAY);
    if (ret != ESP_OK) {
        return ret;
    }

    size_t bytes_written = fwrite(audio_buffer, 1, bytes_read, audio_file);
    return (bytes_written == bytes_read) ? ESP_OK : ESP_FAIL;
}
#endif

// ---------------------------------------------------------------------------
// Minimal AVI (MJPEG, video-only) writer.
//
// File layout produced (224-byte fixed header, then frames, then idx1):
//
//   RIFF <size> AVI
//     LIST <192> hdrl
//       avih <56>   MainAVIHeader          (dwMicroSecPerFrame, totals, ...)
//       LIST <116> strl
//         strh <56> AVIStreamHeader        (vids / MJPG)
//         strf <40> BITMAPINFOHEADER       (MJPG)
//     LIST <size> movi
//       00dc <len> <JPEG bytes> [pad]      (one entry per frame)
//       ...
//     idx1 <size> <entries*16>             (offset relative to "movi" fourcc)
//
// A handful of fields (RIFF size, movi list size, totals, fps) are patched
// at the end because they depend on the real frame count.
// ---------------------------------------------------------------------------

typedef struct {
    FILE *f;
    long  movi_fourcc_offset;   // absolute file offset of "movi" fourcc
    uint32_t frame_count;
    uint32_t max_chunk_size;
    uint8_t *idx_buf;
    size_t   idx_cap;
    size_t   idx_used;
    bool     idx_overflow;
} avi_writer_t;

static inline void put_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static inline void put_u16_le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static esp_err_t avi_begin(avi_writer_t *w, const char *path,
                           int width, int height, uint32_t init_fps)
{
    memset(w, 0, sizeof(*w));

    w->f = fopen(path, "wb+");
    if (!w->f) {
        ESP_LOGE(TAG, "avi_begin: fopen(%s) failed: errno=%d (%s)",
                 path, errno, strerror(errno));
        return ESP_FAIL;
    }

    uint8_t hdr[224] = {0};

    uint32_t usec_per_frame = (init_fps > 0) ? (1000000U / init_fps) : 100000U;
    uint32_t sugg_buf_size = (uint32_t)width * (uint32_t)height;

    memcpy(hdr + 0, "RIFF", 4);
    put_u32_le(hdr + 4, 0);
    memcpy(hdr + 8, "AVI ", 4);

    memcpy(hdr + 12, "LIST", 4);
    put_u32_le(hdr + 16, 4 + 64 + 12 + 64 + 48);
    memcpy(hdr + 20, "hdrl", 4);

    memcpy(hdr + 24, "avih", 4);
    put_u32_le(hdr + 28, 56);
    put_u32_le(hdr + 32, usec_per_frame);
    put_u32_le(hdr + 36, 0);
    put_u32_le(hdr + 40, 0);
    put_u32_le(hdr + 44, 0x10);
    put_u32_le(hdr + 48, 0);
    put_u32_le(hdr + 52, 0);
    put_u32_le(hdr + 56, 1);
    put_u32_le(hdr + 60, sugg_buf_size);
    put_u32_le(hdr + 64, (uint32_t)width);
    put_u32_le(hdr + 68, (uint32_t)height);

    memcpy(hdr + 88, "LIST", 4);
    put_u32_le(hdr + 92, 4 + 64 + 48);
    memcpy(hdr + 96, "strl", 4);

    memcpy(hdr + 100, "strh", 4);
    put_u32_le(hdr + 104, 56);
    memcpy(hdr + 108, "vids", 4);
    memcpy(hdr + 112, "MJPG", 4);
    put_u32_le(hdr + 116, 0);
    put_u16_le(hdr + 120, 0);
    put_u16_le(hdr + 122, 0);
    put_u32_le(hdr + 124, 0);
    put_u32_le(hdr + 128, usec_per_frame);
    put_u32_le(hdr + 132, 1000000);
    put_u32_le(hdr + 136, 0);
    put_u32_le(hdr + 140, 0);
    put_u32_le(hdr + 144, sugg_buf_size);
    put_u32_le(hdr + 148, 0xFFFFFFFFU);
    put_u32_le(hdr + 152, 0);
    put_u16_le(hdr + 156, 0);
    put_u16_le(hdr + 158, 0);
    put_u16_le(hdr + 160, (uint16_t)width);
    put_u16_le(hdr + 162, (uint16_t)height);

    memcpy(hdr + 164, "strf", 4);
    put_u32_le(hdr + 168, 40);
    put_u32_le(hdr + 172, 40);
    put_u32_le(hdr + 176, (uint32_t)width);
    put_u32_le(hdr + 180, (uint32_t)height);
    put_u16_le(hdr + 184, 1);
    put_u16_le(hdr + 186, 24);
    memcpy(hdr + 188, "MJPG", 4);
    put_u32_le(hdr + 192, (uint32_t)width * (uint32_t)height * 3U);
    put_u32_le(hdr + 196, 0);
    put_u32_le(hdr + 200, 0);
    put_u32_le(hdr + 204, 0);
    put_u32_le(hdr + 208, 0);

    memcpy(hdr + 212, "LIST", 4);
    put_u32_le(hdr + 216, 0);
    memcpy(hdr + 220, "movi", 4);

    if (fwrite(hdr, 1, sizeof(hdr), w->f) != sizeof(hdr)) {
        ESP_LOGE(TAG, "avi_begin: header write failed (errno=%d)", errno);
        fclose(w->f);
        w->f = NULL;
        return ESP_FAIL;
    }

    w->movi_fourcc_offset = 220;
    w->frame_count = 0;
    w->max_chunk_size = 0;

    w->idx_cap = 16 * 1024;
    w->idx_buf = heap_caps_malloc(w->idx_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!w->idx_buf) {
        w->idx_buf = malloc(w->idx_cap);
    }
    if (!w->idx_buf) {
        ESP_LOGE(TAG, "avi_begin: cannot allocate %u bytes for idx buffer",
                 (unsigned)w->idx_cap);
        fclose(w->f);
        w->f = NULL;
        return ESP_ERR_NO_MEM;
    }
    w->idx_used = 0;
    w->idx_overflow = false;
    return ESP_OK;
}

static esp_err_t avi_write_frame(avi_writer_t *w, const uint8_t *jpg, size_t len)
{
    long chunk_pos = ftell(w->f);
    if (chunk_pos < 0) return ESP_FAIL;

    uint8_t tag[8];
    memcpy(tag, "00dc", 4);
    put_u32_le(tag + 4, (uint32_t)len);

    if (fwrite(tag, 1, 8, w->f) != 8) return ESP_FAIL;
    if (fwrite(jpg, 1, len, w->f) != len) return ESP_FAIL;
    if (len & 1) {
        uint8_t pad = 0;
        if (fwrite(&pad, 1, 1, w->f) != 1) return ESP_FAIL;
    }

    if (!w->idx_overflow) {
        if (w->idx_used + 16 > w->idx_cap) {
            w->idx_overflow = true;
            ESP_LOGW(TAG, "AVI index buffer full at frame %"PRIu32
                     " — further frames still written but not indexed",
                     w->frame_count);
        } else {
            uint8_t *e = w->idx_buf + w->idx_used;
            memcpy(e, "00dc", 4);
            put_u32_le(e + 4, 0x10);
            put_u32_le(e + 8, (uint32_t)(chunk_pos - w->movi_fourcc_offset));
            put_u32_le(e + 12, (uint32_t)len);
            w->idx_used += 16;
        }
    }

    w->frame_count++;
    if (len > w->max_chunk_size) w->max_chunk_size = (uint32_t)len;
    return ESP_OK;
}

static esp_err_t avi_end(avi_writer_t *w, uint64_t elapsed_us)
{
    esp_err_t result = ESP_OK;
    long movi_end = ftell(w->f);
    if (movi_end < 0) {
        result = ESP_FAIL;
        goto cleanup;
    }

    uint32_t movi_list_size = (uint32_t)(movi_end - w->movi_fourcc_offset);

    uint8_t idx_hdr[8];
    memcpy(idx_hdr, "idx1", 4);
    put_u32_le(idx_hdr + 4, (uint32_t)w->idx_used);
    if (fwrite(idx_hdr, 1, 8, w->f) != 8) { result = ESP_FAIL; goto cleanup; }
    if (w->idx_used > 0 &&
        fwrite(w->idx_buf, 1, w->idx_used, w->f) != w->idx_used) {
        result = ESP_FAIL;
        goto cleanup;
    }

    long file_end = ftell(w->f);
    if (file_end < 0) { result = ESP_FAIL; goto cleanup; }

    uint32_t usec_per_frame = 100000;
    if (w->frame_count > 0 && elapsed_us > 0) {
        usec_per_frame = (uint32_t)(elapsed_us / w->frame_count);
        if (usec_per_frame == 0) usec_per_frame = 1;
    }
    uint32_t max_bps = (uint32_t)(((uint64_t)w->max_chunk_size * 1000000ULL) /
                                  (usec_per_frame ? usec_per_frame : 1));

    uint8_t u32[4];

    #define PATCH_U32(off, val)                                         \
        do {                                                            \
            if (fseek(w->f, (off), SEEK_SET) != 0) {                    \
                result = ESP_FAIL; goto cleanup;                        \
            }                                                           \
            put_u32_le(u32, (uint32_t)(val));                           \
            if (fwrite(u32, 1, 4, w->f) != 4) {                         \
                result = ESP_FAIL; goto cleanup;                        \
            }                                                           \
        } while (0)

    PATCH_U32(4,   (uint32_t)(file_end - 8));        // RIFF size
    PATCH_U32(32,  usec_per_frame);                  // avih dwMicroSecPerFrame
    PATCH_U32(36,  max_bps);                         // avih dwMaxBytesPerSec
    PATCH_U32(48,  w->frame_count);                  // avih dwTotalFrames
    PATCH_U32(60,  w->max_chunk_size + 8);           // avih dwSuggestedBufferSize
    PATCH_U32(128, usec_per_frame);                  // strh dwScale
    PATCH_U32(132, 1000000);                         // strh dwRate
    PATCH_U32(140, w->frame_count);                  // strh dwLength
    PATCH_U32(144, w->max_chunk_size + 8);           // strh dwSuggestedBufferSize
    PATCH_U32(216, movi_list_size);                  // LIST movi size

    #undef PATCH_U32

cleanup:
    if (w->f) {
        fclose(w->f);
        w->f = NULL;
    }
    if (w->idx_buf) {
        free(w->idx_buf);
        w->idx_buf = NULL;
    }
    return result;
}

void record_video(void)
{
    char video_path[40];
#if BOARD_HAS_PDM_MIC
    char audio_path[40];
#endif
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    snprintf(video_path, sizeof(video_path), MOUNT_POINT "/%02d%02d%02d.avi",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
#if BOARD_HAS_PDM_MIC
    snprintf(audio_path, sizeof(audio_path), MOUNT_POINT "/%02d%02d%02d.pcm",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
#endif

    struct stat st;
    if (stat(video_path, &st) == 0) {
        ESP_LOGE(TAG, "Video file already exists: %s", video_path);
        return;
    }
#if BOARD_HAS_PDM_MIC
    if (stat(audio_path, &st) == 0) {
        ESP_LOGE(TAG, "Audio file already exists: %s", audio_path);
        return;
    }
#endif

    uint32_t init_fps = (g_video.frame_interval_ms > 0)
                            ? (1000U / g_video.frame_interval_ms)
                            : 10U;
    avi_writer_t avi;
    if (avi_begin(&avi, video_path, g_video.width, g_video.height, init_fps) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize AVI writer for %s", video_path);
        return;
    }

#if BOARD_HAS_PDM_MIC
    FILE *audio_file = fopen(audio_path, "wb");
    if (!audio_file) {
        ESP_LOGE(TAG, "Failed to open audio file %s: errno=%d (%s)",
                 audio_path, errno, strerror(errno));
        avi_end(&avi, 0);
        return;
    }
#endif

    ESP_LOGI(TAG, "Starting recording (%s %dx%d, quality=%d, %"PRIu32"s, fps_cap=%s)...",
             current_resolution_name(), g_video.width, g_video.height,
             g_video.jpeg_quality, g_video.duration_sec,
             g_video.frame_interval_ms == 0 ? "off" : "on");
    uint64_t start_time = esp_timer_get_time();
    const uint64_t RECORD_LENGTH = (uint64_t)g_video.duration_sec * 1000000ULL;
    uint32_t write_errors = 0;
    uint32_t capture_fail_streak = 0;
    const uint32_t CAPTURE_FAIL_STREAK_ABORT = 50;

    while ((esp_timer_get_time() - start_time) < RECORD_LENGTH) {
        uint64_t frame_start = esp_timer_get_time();

        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            capture_fail_streak++;
            if (capture_fail_streak <= 3 ||
                (capture_fail_streak % 10) == 0) {
                ESP_LOGE(TAG, "Camera capture failed (streak=%" PRIu32 ")",
                         capture_fail_streak);
            }
            if (capture_fail_streak >= CAPTURE_FAIL_STREAK_ABORT) {
                ESP_LOGE(TAG, "Aborting record: %" PRIu32
                         " consecutive capture failures. "
                         "Try `set resolution` to a lower value or power-cycle the board.",
                         capture_fail_streak);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        capture_fail_streak = 0;
        if (fb->format != PIXFORMAT_JPEG) {
            ESP_LOGE(TAG, "Frame format is not JPEG (got %d)", fb->format);
            esp_camera_fb_return(fb);
            break;
        }

        if (avi_write_frame(&avi, fb->buf, fb->len) != ESP_OK) {
            write_errors++;
            ESP_LOGE(TAG, "Failed to write frame (errno=%d)", errno);
        } else if ((avi.frame_count % 30) == 0) {
            ESP_LOGI(TAG, "Recorded %"PRIu32" frames", avi.frame_count);
        }

        esp_camera_fb_return(fb);

#if BOARD_HAS_PDM_MIC
        if (record_audio_chunk(audio_file) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to record audio chunk");
        }
#endif

        // Optional fps cap: if a target interval is configured, sleep the
        // remainder of the frame slot after capture + write.
        if (g_video.frame_interval_ms > 0) {
            uint64_t elapsed_ms =
                (esp_timer_get_time() - frame_start) / 1000ULL;
            if (elapsed_ms < g_video.frame_interval_ms) {
                vTaskDelay(pdMS_TO_TICKS(g_video.frame_interval_ms - elapsed_ms));
            }
        }
    }

    uint64_t elapsed_us = esp_timer_get_time() - start_time;
    uint32_t total_frames = avi.frame_count;
    esp_err_t end_res = avi_end(&avi, elapsed_us);

#if BOARD_HAS_PDM_MIC
    fclose(audio_file);
#endif

    if (end_res != ESP_OK) {
        ESP_LOGE(TAG, "avi_end failed, file may be unplayable");
    }

    float fps = (elapsed_us > 0)
                    ? ((float)total_frames * 1000000.0f / (float)elapsed_us)
                    : 0.0f;
    ESP_LOGI(TAG, "Recording finished. %"PRIu32" frames in %.2f s -> %.2f fps (%"PRIu32" write errors)",
             total_frames, elapsed_us / 1000000.0f, fps, write_errors);

    if (stat(video_path, &st) == 0) {
        ESP_LOGI(TAG, "AVI file: %s (%ld bytes)", video_path, (long)st.st_size);
    }
#if BOARD_HAS_PDM_MIC
    if (stat(audio_path, &st) == 0) {
        ESP_LOGI(TAG, "Audio file: %s (%ld bytes)", audio_path, (long)st.st_size);
    }
#endif
}

static void capture_photo_max(void)
{
    sensor_t *s = esp_camera_sensor_get();
    if (!s) {
        ESP_LOGE(TAG, "capture_photo_max: sensor unavailable");
        return;
    }

    // Preserve runtime video configuration and restore after still capture.
    const framesize_t prev_framesize = g_video.framesize;
    const int prev_quality = g_video.jpeg_quality;
    const int prev_width = g_video.width;
    const int prev_height = g_video.height;

    const resolution_entry_t *target = find_resolution("qxga");
    if (!target) {
        ESP_LOGE(TAG, "capture_photo_max: qxga resolution not available");
        return;
    }

    if (s->set_framesize(s, target->fs) != 0) {
        const resolution_entry_t *fallback = find_resolution("uxga");
        if (!fallback || s->set_framesize(s, fallback->fs) != 0) {
            ESP_LOGE(TAG, "capture_photo_max: cannot switch to qxga/uxga");
            return;
        }
        target = fallback;
        ESP_LOGW(TAG, "capture_photo_max: qxga failed, fallback to uxga");
    }

    const int still_quality = 8; // Lower number = better JPEG quality.
    if (s->set_quality(s, still_quality) != 0) {
        ESP_LOGE(TAG, "capture_photo_max: set_quality(%d) failed", still_quality);
        goto restore;
    }

    // Allow sensor PLL/JPEG pipeline to settle after framesize switch.
    vTaskDelay(pdMS_TO_TICKS(1500));

    char photo_path[40];
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    snprintf(photo_path, sizeof(photo_path), MOUNT_POINT "/%02d%02d%02d.jpg",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    struct stat st;
    if (stat(photo_path, &st) == 0) {
        ESP_LOGE(TAG, "Photo file already exists: %s", photo_path);
        goto restore;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "capture_photo_max: Camera capture failed");
        goto restore;
    }
    if (fb->format != PIXFORMAT_JPEG) {
        ESP_LOGE(TAG, "capture_photo_max: frame not JPEG (format=%d)", fb->format);
        esp_camera_fb_return(fb);
        goto restore;
    }

    FILE *photo = fopen(photo_path, "wb");
    if (!photo) {
        ESP_LOGE(TAG, "capture_photo_max: fopen(%s) failed: errno=%d (%s)",
                 photo_path, errno, strerror(errno));
        esp_camera_fb_return(fb);
        goto restore;
    }

    size_t written = fwrite(fb->buf, 1, fb->len, photo);
    fclose(photo);
    esp_camera_fb_return(fb);

    if (written != fb->len) {
        ESP_LOGE(TAG, "capture_photo_max: short write (%u/%u bytes)",
                 (unsigned)written, (unsigned)fb->len);
        goto restore;
    }

    if (stat(photo_path, &st) == 0) {
        ESP_LOGI(TAG, "Photo saved: %s (%ld bytes) at %s (%dx%d, q=%d)",
                 photo_path, (long)st.st_size, target->name,
                 target->width, target->height, still_quality);
    } else {
        ESP_LOGI(TAG, "Photo saved: %s at %s (%dx%d, q=%d)",
                 photo_path, target->name, target->width, target->height, still_quality);
    }

restore:
    // Restore original runtime video settings for subsequent recording.
    if (s->set_framesize(s, prev_framesize) != 0) {
        ESP_LOGW(TAG, "capture_photo_max: failed to restore framesize");
    }
    if (s->set_quality(s, prev_quality) != 0) {
        ESP_LOGW(TAG, "capture_photo_max: failed to restore quality");
    }
    g_video.framesize = prev_framesize;
    g_video.width = prev_width;
    g_video.height = prev_height;
    g_video.jpeg_quality = prev_quality;
    vTaskDelay(pdMS_TO_TICKS(300));
}

static void capture_photo_all_resolutions(void)
{
    sensor_t *s = esp_camera_sensor_get();
    if (!s) {
        ESP_LOGE(TAG, "capture_photo_all_resolutions: sensor unavailable");
        return;
    }
    if (!s_sd_card) {
        ESP_LOGE(TAG, "capture_photo_all_resolutions: SD card not available");
        return;
    }

    // Preserve runtime video configuration and restore after the test sweep.
    const framesize_t prev_framesize = g_video.framesize;
    const int prev_quality = g_video.jpeg_quality;
    const int prev_width = g_video.width;
    const int prev_height = g_video.height;
    const int still_quality = 8;
    camera_fb_t *fb = NULL;
    const resolution_entry_t *r = find_resolution("qxga");
    if (!r) {
        ESP_LOGE(TAG, "capture_photo_all_resolutions: qxga resolution not available");
        goto restore;
    }

    ESP_LOGI(TAG, "BOOT action: fixed capture at qxga");

    if (s->set_framesize(s, r->fs) != 0) {
        ESP_LOGW(TAG, "Skip %s: set_framesize failed", r->name);
        goto restore;
    }
    if (s->set_quality(s, still_quality) != 0) {
        ESP_LOGW(TAG, "Skip %s: set_quality(%d) failed", r->name, still_quality);
        goto restore;
    }

    // Give sensor time to settle, then drop early unstable frames.
    vTaskDelay(pdMS_TO_TICKS(1200));
    for (int warm = 0; warm < 2; warm++) {
        camera_fb_t *warm_fb = esp_camera_fb_get();
        if (!warm_fb) {
            break;
        }
        esp_camera_fb_return(warm_fb);
        vTaskDelay(pdMS_TO_TICKS(40));
    }

    fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGW(TAG, "Skip %s: capture failed", r->name);
        goto restore;
    }
    if (fb->format != PIXFORMAT_JPEG) {
        ESP_LOGW(TAG, "Skip %s: frame format=%d (not JPEG)", r->name, fb->format);
        esp_camera_fb_return(fb);
        goto restore;
    }

    time_t now;
    struct tm timeinfo;
    char photo_path[64];
    time(&now);
    localtime_r(&now, &timeinfo);
    uint32_t ms = (uint32_t)((esp_timer_get_time() / 1000ULL) % 1000ULL);
    snprintf(photo_path, sizeof(photo_path),
             MOUNT_POINT "/%02d%02d%02d_%03lu_%s.jpg",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
             (unsigned long)ms, r->name);

    FILE *photo = fopen(photo_path, "wb");
    if (!photo) {
        ESP_LOGW(TAG, "Skip %s: fopen failed errno=%d (%s)",
                 r->name, errno, strerror(errno));
        esp_camera_fb_return(fb);
        goto restore;
    }

    size_t fb_len = fb->len;
    size_t written = fwrite(fb->buf, 1, fb_len, photo);
    fclose(photo);
    esp_camera_fb_return(fb);
    fb = NULL;

    if (written != fb_len) {
        ESP_LOGW(TAG, "Skip %s: short write (%u/%u)",
                 r->name, (unsigned)written, (unsigned)fb_len);
        goto restore;
    }

    struct stat st;
    if (stat(photo_path, &st) == 0) {
        ESP_LOGI(TAG, "Saved [%s] %dx%d q=%d -> %s (%ld bytes)",
                 r->name, r->width, r->height, still_quality,
                 photo_path, (long)st.st_size);
    } else {
        ESP_LOGI(TAG, "Saved [%s] %dx%d q=%d -> %s",
                 r->name, r->width, r->height, still_quality, photo_path);
    }

restore:
    if (fb) {
        esp_camera_fb_return(fb);
    }

    if (s->set_framesize(s, prev_framesize) != 0) {
        ESP_LOGW(TAG, "capture_photo_all_resolutions: failed to restore framesize");
    }
    if (s->set_quality(s, prev_quality) != 0) {
        ESP_LOGW(TAG, "capture_photo_all_resolutions: failed to restore quality");
    }
    g_video.framesize = prev_framesize;
    g_video.width = prev_width;
    g_video.height = prev_height;
    g_video.jpeg_quality = prev_quality;
    vTaskDelay(pdMS_TO_TICKS(300));
}

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
        if ((now_ms - s_last_photo_btn_ms) < PHOTO_BTN_DEBOUNCE_MS) {
            continue;
        }
        s_last_photo_btn_ms = now_ms;

        if ((gpio_num_t)gpio_num != PHOTO_BTN_GPIO) {
            continue;
        }
        if (gpio_get_level(PHOTO_BTN_GPIO) != PHOTO_BTN_ACTIVE_LEVEL) {
            continue;
        }

        ESP_LOGI(TAG, "BOOT button pressed -> capture fixed qxga photo");
        capture_photo_all_resolutions();
    }
}

static esp_err_t init_photo_button(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PHOTO_BTN_GPIO),
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

    ESP_ERROR_CHECK(gpio_isr_handler_add(PHOTO_BTN_GPIO, photo_btn_isr_handler,
                                         (void *)PHOTO_BTN_GPIO));

    BaseType_t task_ok = xTaskCreate(photo_btn_task, "photo_btn_task", 4096,
                                     NULL, 5, &s_photo_btn_task);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create photo_btn_task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Photo button ready on GPIO%d (BOOT)", (int)PHOTO_BTN_GPIO);
    return ESP_OK;
}

static void build_vfs_path(char *out, size_t out_sz, const char *user_path)
{
    if (user_path[0] == '/') {
        snprintf(out, out_sz, "%s", user_path);
    } else {
        snprintf(out, out_sz, "%s/%s", MOUNT_POINT, user_path);
    }
}

static void handle_transfer_command(const char* file_path)
{
    char full_path[64];
    build_vfs_path(full_path, sizeof(full_path), file_path);

    FILE *file = fopen(full_path, "rb");
    if (!file) {
        printf("Error: Could not open file %s: errno=%d (%s)\n",
               full_path, errno, strerror(errno));
        return;
    }

    struct stat st;
    if (stat(full_path, &st) != 0) {
        printf("Error: Could not get file info: errno=%d\n", errno);
        fclose(file);
        return;
    }

    long file_size = (long)st.st_size;
    printf("File size: %ld bytes\n", file_size);
    printf("Transfer starting...\n");

    uint8_t buffer[1024];
    size_t bytes_read;
    long total_bytes = 0;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        for (size_t i = 0; i < bytes_read; i++) {
            printf("%02x", buffer[i]);
        }
        total_bytes += (long)bytes_read;

        if (file_size > 0 && total_bytes % (64 * 1024) == 0) {
            printf("\nTransferred: %ld bytes (%.1f%%)\n",
                   total_bytes, (total_bytes * 100.0f) / file_size);
        }
    }

    printf("\nTransfer complete: %ld bytes transferred\n", total_bytes);
    fclose(file);
}

static int console_handler(int argc, char **argv)
{
    if (argc < 1) {
        return 0;
    }

    if (strcmp(argv[0], "record") == 0) {
        record_video();
    } else if (strcmp(argv[0], "photo") == 0) {
        capture_photo_max();
    } else if (strcmp(argv[0], "transfer") == 0) {
        if (argc != 2) {
            printf("Usage: transfer <filename>\n");
            return 0;
        }
        handle_transfer_command(argv[1]);
    } else if (strcmp(argv[0], "ls") == 0) {
        DIR *dir = opendir(MOUNT_POINT);
        if (!dir) {
            printf("Error: Could not open %s (errno=%d: %s)\n",
                   MOUNT_POINT, errno, strerror(errno));
            return 0;
        }

        printf("Files on SD (%s):\n", MOUNT_POINT);
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_type == DT_DIR) {
                continue;
            }
            char full[288];
            snprintf(full, sizeof(full), "%s/%s", MOUNT_POINT, ent->d_name);
            struct stat st;
            if (stat(full, &st) == 0) {
                printf("%s\t%ld bytes\n", ent->d_name, (long)st.st_size);
            } else {
                printf("%s\t?\n", ent->d_name);
            }
        }
        closedir(dir);

    } else if (strcmp(argv[0], "show") == 0) {
        printf("Video configuration:\n");
        printf("  resolution = %s (%dx%d)\n",
               current_resolution_name(), g_video.width, g_video.height);
        printf("  quality    = %d (0..63, lower = better)\n", g_video.jpeg_quality);
        printf("  duration   = %"PRIu32" seconds\n", g_video.duration_sec);
        if (g_video.frame_interval_ms == 0) {
            printf("  fps cap    = off (max fps)\n");
        } else {
            printf("  fps cap    = %"PRIu32" ms/frame (~%.1f fps)\n",
                   g_video.frame_interval_ms,
                   1000.0f / (float)g_video.frame_interval_ms);
        }

    } else if (strcmp(argv[0], "set") == 0) {
        if (argc != 3) {
            printf("Usage:\n");
            printf("  set resolution <qqvga|qvga|cif|hvga|vga|svga|xga|hd|sxga|uxga|fhd|qxga>\n");
            printf("  set quality <0..63>          (lower = better quality, bigger file)\n");
            printf("  set duration <1..3600>       (seconds)\n");
            printf("  set fps <0..30>              (0 = max, else cap)\n");
            return 0;
        }

        if (strcmp(argv[1], "resolution") == 0) {
            const resolution_entry_t *r = find_resolution(argv[2]);
            if (!r) {
                printf("Unknown resolution '%s'. Use one of: "
                       "qqvga qvga cif hvga vga svga xga hd sxga uxga fhd qxga\n",
                       argv[2]);
                return 0;
            }
            g_video.framesize = r->fs;
            g_video.width = r->width;
            g_video.height = r->height;
            if (apply_video_config() != ESP_OK) {
                printf("Failed to apply framesize (camera may be busy)\n");
                return 0;
            }
            printf("Resolution -> %s (%dx%d)\n", r->name, r->width, r->height);

        } else if (strcmp(argv[1], "quality") == 0) {
            int q = atoi(argv[2]);
            if (q < 0 || q > 63) {
                printf("quality must be 0..63 (lower = better)\n");
                return 0;
            }
            g_video.jpeg_quality = q;
            if (apply_quality_only(q) != ESP_OK) {
                printf("Failed to apply quality\n");
                return 0;
            }
            printf("Quality -> %d\n", q);

        } else if (strcmp(argv[1], "duration") == 0) {
            int d = atoi(argv[2]);
            if (d <= 0 || d > 3600) {
                printf("duration must be 1..3600 seconds\n");
                return 0;
            }
            g_video.duration_sec = (uint32_t)d;
            printf("Duration -> %d s\n", d);

        } else if (strcmp(argv[1], "fps") == 0) {
            int fps = atoi(argv[2]);
            if (fps < 0 || fps > 30) {
                printf("fps must be 0..30 (0 = uncapped max)\n");
                return 0;
            }
            g_video.frame_interval_ms = (fps == 0) ? 0U : (1000U / (uint32_t)fps);
            if (fps == 0) {
                printf("FPS cap -> off (max)\n");
            } else {
                printf("FPS cap -> ~%d fps (interval %"PRIu32" ms)\n",
                       fps, g_video.frame_interval_ms);
            }

        } else {
            printf("Unknown parameter '%s'. Run `set` with no args for usage.\n",
                   argv[1]);
        }
    }

    return 0;
}

static const char *chip_model_str(esp_chip_model_t model)
{
    switch (model) {
        case CHIP_ESP32:     return "ESP32";
        case CHIP_ESP32S2:   return "ESP32-S2";
        case CHIP_ESP32S3:   return "ESP32-S3";
        case CHIP_ESP32C3:   return "ESP32-C3";
        case CHIP_ESP32C2:   return "ESP32-C2";
        case CHIP_ESP32C6:   return "ESP32-C6";
        case CHIP_ESP32H2:   return "ESP32-H2";
        case CHIP_ESP32P4:   return "ESP32-P4";
        case CHIP_POSIX_LINUX: return "POSIX/Linux";
        default:             return "unknown";
    }
}

static void print_chip_info(void)
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
           (info.features & CHIP_FEATURE_IEEE802154)? "802.15.4" : "");
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

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_camera_init(&camera_config));
    ESP_LOGI(TAG, "Camera initialized: %s %dx%d q=%d (camera_config matches g_video default)",
             current_resolution_name(), g_video.width, g_video.height,
             g_video.jpeg_quality);

#if BOARD_HAS_PDM_MIC
    ESP_ERROR_CHECK(init_i2s());
    ESP_LOGI(TAG, "I2S initialized");
#endif

    esp_err_t sd_ret = init_sdcard();
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
    print_chip_info();

    esp_err_t btn_ret = init_photo_button();
    if (btn_ret != ESP_OK) {
        ESP_LOGW(TAG, "Photo button init failed (%s); command `photo` still works",
                 esp_err_to_name(btn_ret));
    }

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "esp32> ";
    repl_config.max_cmdline_length = 64;
    // Default REPL stack (4 KiB) is too small once we open FatFS files and
    // log through printf from inside command handlers. Bump it to avoid
    // stack overflow corrupting the UART driver mutex.
    repl_config.task_stack_size = 8192;

    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

    esp_console_cmd_t cmd = {
        .command = "record",
#if BOARD_HAS_PDM_MIC
        .help = "Start recording video and audio",
#else
        .help = "Start recording video to SD",
#endif
        .hint = NULL,
        .func = &console_handler,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));

    cmd.command = "photo";
    cmd.help = "Capture one max-resolution JPEG photo to SD";
    cmd.hint = NULL;
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));

    cmd.command = "transfer";
    cmd.help = "Transfer a file in hex format";
    cmd.hint = "<filename>";
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));

    cmd.command = "ls";
    cmd.help = "List files on SD card";
    cmd.hint = NULL;
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));

    cmd.command = "show";
    cmd.help = "Show current video configuration";
    cmd.hint = NULL;
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));

    cmd.command = "set";
    cmd.help = "Set a video parameter (run `set` with no args for usage)";
    cmd.hint = "<param> <value>";
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));

    ESP_ERROR_CHECK(esp_console_start_repl(repl));

#if BOARD_HAS_PDM_MIC
    atexit(deinit_i2s);
#endif
}
