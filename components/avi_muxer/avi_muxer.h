#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct avi_writer avi_writer_t;

esp_err_t avi_begin(const char *path, int width, int height, uint32_t init_fps,
                    avi_writer_t **out_writer);
esp_err_t avi_write_frame(avi_writer_t *writer, const uint8_t *jpg, size_t len);
esp_err_t avi_end(avi_writer_t *writer, uint64_t elapsed_us);
void avi_abort(avi_writer_t *writer);
uint32_t avi_frame_count(const avi_writer_t *writer);
