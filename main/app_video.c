#include "app_video.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include "avi_muxer.h"
#include "bsp_board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "audio_hal.h"
#include "camera_hal.h"

static app_video_config_t g_video = {
    .framesize = FRAMESIZE_SVGA,
    .width = 800,
    .height = 600,
    .jpeg_quality = 12,
    .duration_sec = 30,
    .frame_interval_ms = 0,
};

static const app_video_resolution_t g_resolutions[] = {
    {"qqvga", FRAMESIZE_QQVGA, 160, 120},
    {"qvga", FRAMESIZE_QVGA, 320, 240},
    {"cif", FRAMESIZE_CIF, 400, 296},
    {"hvga", FRAMESIZE_HVGA, 480, 320},
    {"vga", FRAMESIZE_VGA, 640, 480},
    {"svga", FRAMESIZE_SVGA, 800, 600},
    {"xga", FRAMESIZE_XGA, 1024, 768},
    {"hd", FRAMESIZE_HD, 1280, 720},
    {"sxga", FRAMESIZE_SXGA, 1280, 1024},
    {"uxga", FRAMESIZE_UXGA, 1600, 1200},
    {"fhd", FRAMESIZE_FHD, 1920, 1080},
    {"qxga", FRAMESIZE_QXGA, 2048, 1536},
};
static const size_t g_resolutions_count = sizeof(g_resolutions) / sizeof(g_resolutions[0]);
static const char *TAG = "app_video";
static sdcard_hal_t *s_sdcard = NULL;

static esp_err_t build_unique_media_path(char *out, size_t out_sz, const char *ext)
{
    if (!out || out_sz == 0 || !ext) {
        return ESP_ERR_INVALID_ARG;
    }
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    uint32_t ms = (uint32_t)((esp_timer_get_time() / 1000ULL) % 1000ULL);
    for (int seq = 0; seq < 100; seq++) {
        if (seq == 0) {
            snprintf(out, out_sz, BSP_SD_MOUNT_POINT "/%02d%02d%02d_%03lu%s",
                     timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                     (unsigned long)ms, ext);
        } else {
            snprintf(out, out_sz, BSP_SD_MOUNT_POINT "/%02d%02d%02d_%03lu_%02d%s",
                     timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                     (unsigned long)ms, seq, ext);
        }
        struct stat st;
        if (stat(out, &st) != 0) {
            return ESP_OK;
        }
    }
    return ESP_ERR_INVALID_STATE;
}

static bool still_jpeg_soi_ok(const camera_fb_t *fb)
{
    if (!fb || fb->format != PIXFORMAT_JPEG || fb->len < 4) {
        return false;
    }
    return fb->buf[0] == 0xff && fb->buf[1] == 0xd8;
}

static void still_capture_warmup(uint32_t settle_ms)
{
    vTaskDelay(pdMS_TO_TICKS(settle_ms));
}

static camera_fb_t *still_capture_retry(const char *ctx, int attempts, uint32_t delay_ms)
{
    camera_fb_t *fb = NULL;
    for (int attempt = 0; attempt < attempts; attempt++) {
        if (attempt > 0) {
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
        fb = camera_hal_get_frame();
        if (still_jpeg_soi_ok(fb)) {
            return fb;
        }
        if (fb) {
            camera_hal_return_frame(fb);
            fb = NULL;
        }
        ESP_LOGW(TAG, "%s: invalid frame on attempt %d/%d", ctx, attempt + 1, attempts);
    }
    return NULL;
}

static camera_fb_t *still_capture_two_phase(const char *ctx, sensor_t *s, framesize_t fs, int quality)
{
    camera_fb_t *fb = still_capture_retry(ctx, 5, 300);
    if (fb) {
        return fb;
    }
    ESP_LOGW(TAG, "%s: phase-1 failed, re-apply sensor settings", ctx);
    if (s->set_framesize(s, fs) != 0 || s->set_quality(s, quality) != 0) {
        ESP_LOGE(TAG, "%s: failed to re-apply still settings", ctx);
        return NULL;
    }
    still_capture_warmup(1500);
    return still_capture_retry(ctx, 8, 400);
}

esp_err_t app_video_init(void)
{
    return ESP_OK;
}

void app_video_set_sdcard(sdcard_hal_t *sdcard)
{
    s_sdcard = sdcard;
}

const app_video_config_t *app_video_get_config(void)
{
    return &g_video;
}

app_video_config_t *app_video_get_config_mutable(void)
{
    return &g_video;
}

const app_video_resolution_t *app_video_find_resolution(const char *name)
{
    if (!name) {
        return NULL;
    }
    for (size_t i = 0; i < g_resolutions_count; i++) {
        if (strcasecmp(g_resolutions[i].name, name) == 0) {
            return &g_resolutions[i];
        }
    }
    return NULL;
}

const char *app_video_resolution_name(void)
{
    for (size_t i = 0; i < g_resolutions_count; i++) {
        if (g_resolutions[i].fs == g_video.framesize) {
            return g_resolutions[i].name;
        }
    }
    return "?";
}

esp_err_t app_video_apply_config(void)
{
    const app_video_config_t *cfg_src = app_video_get_config();
    camera_hal_config_t cfg = {
        .framesize = cfg_src->framesize,
        .jpeg_quality = cfg_src->jpeg_quality,
        .fb_count = 2,
    };
    return camera_hal_apply_config(&cfg);
}

esp_err_t app_video_apply_quality(int quality)
{
    return camera_hal_set_quality(quality);
}

esp_err_t app_video_record(void)
{
    if (!s_sdcard || !sdcard_hal_is_ready(s_sdcard)) {
        return ESP_ERR_INVALID_STATE;
    }
    char video_path[64];
#if BOARD_HAS_PDM_MIC
    char audio_path[64];
#endif
    if (build_unique_media_path(video_path, sizeof(video_path), ".avi") != ESP_OK) {
        ESP_LOGE(TAG, "Failed to allocate unique video path");
        return ESP_FAIL;
    }
#if BOARD_HAS_PDM_MIC
    if (build_unique_media_path(audio_path, sizeof(audio_path), ".pcm") != ESP_OK) {
        ESP_LOGE(TAG, "Failed to allocate unique audio path");
        return ESP_FAIL;
    }
#endif

    const app_video_config_t *cfg = app_video_get_config();
    uint32_t init_fps = (cfg->frame_interval_ms > 0) ? (1000U / cfg->frame_interval_ms) : 10U;
    avi_writer_t *avi = NULL;
    if (avi_begin(video_path, cfg->width, cfg->height, init_fps, &avi) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize AVI writer for %s", video_path);
        return ESP_FAIL;
    }
#if BOARD_HAS_PDM_MIC
    FILE *audio_file = fopen(audio_path, "wb");
    if (!audio_file) {
        ESP_LOGE(TAG, "Failed to open audio file %s: errno=%d (%s)", audio_path, errno, strerror(errno));
        avi_abort(avi);
        return ESP_FAIL;
    }
#endif
    uint64_t start_time = esp_timer_get_time();
    const uint64_t record_length = (uint64_t)cfg->duration_sec * 1000000ULL;
    uint32_t write_errors = 0;
    uint32_t capture_fail_streak = 0;
    const uint32_t capture_fail_streak_abort = 50;
    while ((esp_timer_get_time() - start_time) < record_length) {
        uint64_t frame_start = esp_timer_get_time();
        camera_fb_t *fb = camera_hal_get_frame();
        if (!fb) {
            capture_fail_streak++;
            if (capture_fail_streak >= capture_fail_streak_abort) {
                ESP_LOGE(TAG, "Aborting record after %u capture failures", capture_fail_streak);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        capture_fail_streak = 0;
        if (fb->format != PIXFORMAT_JPEG) {
            camera_hal_return_frame(fb);
            break;
        }
        if (avi_write_frame(avi, fb->buf, fb->len) != ESP_OK) {
            write_errors++;
        }
        camera_hal_return_frame(fb);
#if BOARD_HAS_PDM_MIC
        (void)audio_hal_record_chunk(audio_file);
#endif
        if (cfg->frame_interval_ms > 0) {
            uint64_t elapsed_ms = (esp_timer_get_time() - frame_start) / 1000ULL;
            if (elapsed_ms < cfg->frame_interval_ms) {
                vTaskDelay(pdMS_TO_TICKS(cfg->frame_interval_ms - elapsed_ms));
            }
        }
    }
    uint64_t elapsed_us = esp_timer_get_time() - start_time;
    uint32_t total_frames = avi_frame_count(avi);
    esp_err_t end_res = avi_end(avi, elapsed_us);
    struct stat st;
#if BOARD_HAS_PDM_MIC
    fclose(audio_file);
#endif
    ESP_LOGI(TAG, "Recording finished: frames=%" PRIu32 " write_errors=%" PRIu32, total_frames, write_errors);
    if (stat(video_path, &st) == 0) {
        ESP_LOGI(TAG, "Video saved: %s (%ld bytes)", video_path, (long)st.st_size);
    } else {
        ESP_LOGI(TAG, "Video saved: %s", video_path);
    }
#if BOARD_HAS_PDM_MIC
    if (stat(audio_path, &st) == 0) {
        ESP_LOGI(TAG, "Audio saved: %s (%ld bytes)", audio_path, (long)st.st_size);
    } else {
        ESP_LOGI(TAG, "Audio saved: %s", audio_path);
    }
#endif
    return (end_res == ESP_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t app_video_capture_photo(void)
{
    if (!s_sdcard || !sdcard_hal_is_ready(s_sdcard)) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = ESP_FAIL;
    sensor_t *s = camera_hal_get_sensor();
    if (!s) {
        return ESP_FAIL;
    }
    app_video_config_t *cfg = app_video_get_config_mutable();
    const framesize_t prev_framesize = cfg->framesize;
    const int prev_quality = cfg->jpeg_quality;
    const int prev_width = cfg->width;
    const int prev_height = cfg->height;
    const app_video_resolution_t *target = app_video_find_resolution("qxga");
    if (!target) {
        return ESP_FAIL;
    }
    if (s->set_framesize(s, target->fs) != 0) {
        const app_video_resolution_t *fallback = app_video_find_resolution("uxga");
        if (!fallback || s->set_framesize(s, fallback->fs) != 0) {
            return ESP_FAIL;
        }
        target = fallback;
    }
    const int still_quality = 12;
    if (s->set_quality(s, still_quality) != 0) {
        goto restore;
    }
    still_capture_warmup(8000);
    char photo_path[64];
    if (build_unique_media_path(photo_path, sizeof(photo_path), ".jpg") != ESP_OK) {
        goto restore;
    }
    camera_fb_t *fb = still_capture_two_phase("capture_photo_max", s, target->fs, still_quality);
    if (!fb || !still_jpeg_soi_ok(fb)) {
        if (fb) camera_hal_return_frame(fb);
        goto restore;
    }
    FILE *photo = fopen(photo_path, "wb");
    if (!photo) {
        camera_hal_return_frame(fb);
        goto restore;
    }
    size_t fb_len = fb->len;
    size_t written = fwrite(fb->buf, 1, fb_len, photo);
    fclose(photo);
    camera_hal_return_frame(fb);
    if (written != fb_len) {
        goto restore;
    }
    struct stat st;
    if (stat(photo_path, &st) == 0) {
        ESP_LOGI(TAG, "Photo saved: %s (%ld bytes)", photo_path, (long)st.st_size);
    } else {
        ESP_LOGI(TAG, "Photo saved: %s", photo_path);
    }
    result = ESP_OK;
restore:
    (void)s->set_framesize(s, prev_framesize);
    (void)s->set_quality(s, prev_quality);
    cfg->framesize = prev_framesize;
    cfg->width = prev_width;
    cfg->height = prev_height;
    cfg->jpeg_quality = prev_quality;
    vTaskDelay(pdMS_TO_TICKS(300));
    return result;
}
