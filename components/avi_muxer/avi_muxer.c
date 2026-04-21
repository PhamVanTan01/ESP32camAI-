#include "avi_muxer.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "avi_muxer";

struct avi_writer {
    FILE *f;
    long movi_fourcc_offset;
    uint32_t frame_count;
    uint32_t max_chunk_size;
    uint8_t *idx_buf;
    size_t idx_cap;
    size_t idx_used;
    bool idx_overflow;
};

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

static void avi_free_writer(avi_writer_t *w)
{
    if (!w) {
        return;
    }
    if (w->f) {
        fclose(w->f);
        w->f = NULL;
    }
    if (w->idx_buf) {
        free(w->idx_buf);
        w->idx_buf = NULL;
    }
    free(w);
}

esp_err_t avi_begin(const char *path, int width, int height, uint32_t init_fps,
                    avi_writer_t **out_writer)
{
    if (!path || !out_writer) {
        return ESP_ERR_INVALID_ARG;
    }

    avi_writer_t *w = calloc(1, sizeof(*w));
    if (!w) {
        return ESP_ERR_NO_MEM;
    }

    w->f = fopen(path, "wb+");
    if (!w->f) {
        ESP_LOGE(TAG, "avi_begin: fopen(%s) failed: errno=%d (%s)",
                 path, errno, strerror(errno));
        avi_free_writer(w);
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
        avi_free_writer(w);
        return ESP_FAIL;
    }

    w->movi_fourcc_offset = 220;
    w->idx_cap = 16 * 1024;
    w->idx_buf = heap_caps_malloc(w->idx_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!w->idx_buf) {
        w->idx_buf = malloc(w->idx_cap);
    }
    if (!w->idx_buf) {
        ESP_LOGE(TAG, "avi_begin: cannot allocate %u bytes for idx buffer",
                 (unsigned)w->idx_cap);
        avi_free_writer(w);
        return ESP_ERR_NO_MEM;
    }

    *out_writer = w;
    return ESP_OK;
}

esp_err_t avi_write_frame(avi_writer_t *w, const uint8_t *jpg, size_t len)
{
    if (!w || !w->f || !jpg || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

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

esp_err_t avi_end(avi_writer_t *w, uint64_t elapsed_us)
{
    if (!w || !w->f) {
        return ESP_ERR_INVALID_ARG;
    }

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

    PATCH_U32(4, (uint32_t)(file_end - 8));
    PATCH_U32(32, usec_per_frame);
    PATCH_U32(36, max_bps);
    PATCH_U32(48, w->frame_count);
    PATCH_U32(60, w->max_chunk_size + 8);
    PATCH_U32(128, usec_per_frame);
    PATCH_U32(132, 1000000);
    PATCH_U32(140, w->frame_count);
    PATCH_U32(144, w->max_chunk_size + 8);
    PATCH_U32(216, movi_list_size);

    #undef PATCH_U32

cleanup:
    avi_free_writer(w);
    return result;
}

void avi_abort(avi_writer_t *writer)
{
    avi_free_writer(writer);
}

uint32_t avi_frame_count(const avi_writer_t *writer)
{
    return writer ? writer->frame_count : 0;
}
