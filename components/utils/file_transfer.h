#pragma once

#include <stddef.h>
#include "esp_err.h"

void file_transfer_build_vfs_path(char *out, size_t out_sz,
                                  const char *mount_point,
                                  const char *user_path);

esp_err_t file_transfer_hex(const char *mount_point, const char *user_path);
