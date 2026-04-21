#include "file_transfer.h"

#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

void file_transfer_build_vfs_path(char *out, size_t out_sz,
                                  const char *mount_point,
                                  const char *user_path)
{
    if (user_path[0] == '/') {
        snprintf(out, out_sz, "%s", user_path);
    } else {
        snprintf(out, out_sz, "%s/%s", mount_point, user_path);
    }
}

esp_err_t file_transfer_hex(const char *mount_point, const char *user_path)
{
    char full_path[64];
    file_transfer_build_vfs_path(full_path, sizeof(full_path), mount_point, user_path);

    FILE *file = fopen(full_path, "rb");
    if (!file) {
        printf("Error: Could not open file %s: errno=%d (%s)\n",
               full_path, errno, strerror(errno));
        return ESP_FAIL;
    }

    struct stat st;
    if (stat(full_path, &st) != 0) {
        printf("Error: Could not get file info: errno=%d\n", errno);
        fclose(file);
        return ESP_FAIL;
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
    return ESP_OK;
}
