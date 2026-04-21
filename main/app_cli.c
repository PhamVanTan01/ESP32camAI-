#include "app_cli.h"

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "bsp_board.h"
#include "file_transfer.h"
#include "app_video.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "driver/uart.h"

static esp_console_repl_t *s_repl = NULL;

static int cmd_record(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (app_video_record() != ESP_OK) {
        printf("record failed\n");
    }
    return 0;
}

static int cmd_photo(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (app_video_capture_photo() != ESP_OK) {
        printf("photo failed\n");
    }
    return 0;
}

static int cmd_transfer(int argc, char **argv)
{
    if (argc != 2) {
        printf("Usage: transfer <filename>\n");
        return 0;
    }
    if (file_transfer_hex(BSP_SD_MOUNT_POINT, argv[1]) != ESP_OK) {
        printf("transfer failed\n");
    }
    return 0;
}

static int cmd_ls(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    DIR *dir = opendir(BSP_SD_MOUNT_POINT);
    if (!dir) {
        printf("Error: Could not open %s (errno=%d: %s)\n",
               BSP_SD_MOUNT_POINT, errno, strerror(errno));
        return 0;
    }

    printf("Files on SD (%s):\n", BSP_SD_MOUNT_POINT);
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_type == DT_DIR) {
            continue;
        }
        char full[288];
        snprintf(full, sizeof(full), "%s/%s", BSP_SD_MOUNT_POINT, ent->d_name);
        struct stat st;
        if (stat(full, &st) == 0) {
            printf("%s\t%ld bytes\n", ent->d_name, (long)st.st_size);
        } else {
            printf("%s\t?\n", ent->d_name);
        }
    }
    closedir(dir);
    return 0;
}

static int cmd_show(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const app_video_config_t *cfg = app_video_get_config();
    printf("Video configuration:\n");
    printf("  resolution = %s (%dx%d)\n",
           app_video_resolution_name(), cfg->width, cfg->height);
    printf("  quality    = %d (0..63, lower = better)\n", cfg->jpeg_quality);
    printf("  duration   = %" PRIu32 " seconds\n", cfg->duration_sec);
    if (cfg->frame_interval_ms == 0) {
        printf("  fps cap    = off (max fps)\n");
    } else {
        printf("  fps cap    = %" PRIu32 " ms/frame (~%.1f fps)\n",
               cfg->frame_interval_ms,
               1000.0f / (float)cfg->frame_interval_ms);
    }
    return 0;
}

static int cmd_set(int argc, char **argv)
{
    if (argc != 3) {
        printf("Usage:\n");
        printf("  set resolution <qqvga|qvga|cif|hvga|vga|svga|xga|hd|sxga|uxga|fhd|qxga>\n");
        printf("  set quality <0..63>          (lower = better quality, bigger file)\n");
        printf("  set duration <1..3600>       (seconds)\n");
        printf("  set fps <0..30>              (0 = max, else cap)\n");
        return 0;
    }

    if (strcmp(argv[1], "resolution") == 0) {
        app_video_config_t *cfg = app_video_get_config_mutable();
        const app_video_resolution_t *r = app_video_find_resolution(argv[2]);
        if (!r) {
            printf("Unknown resolution '%s'. Use one of: "
                   "qqvga qvga cif hvga vga svga xga hd sxga uxga fhd qxga\n",
                   argv[2]);
            return 0;
        }
        cfg->framesize = r->fs;
        cfg->width = r->width;
        cfg->height = r->height;
        if (app_video_apply_config() != ESP_OK) {
            printf("Failed to apply framesize (camera may be busy)\n");
            return 0;
        }
        printf("Resolution -> %s (%dx%d)\n", r->name, r->width, r->height);
    } else if (strcmp(argv[1], "quality") == 0) {
        app_video_config_t *cfg = app_video_get_config_mutable();
        int q = atoi(argv[2]);
        if (q < 0 || q > 63) {
            printf("quality must be 0..63 (lower = better)\n");
            return 0;
        }
        cfg->jpeg_quality = q;
        if (app_video_apply_quality(q) != ESP_OK) {
            printf("Failed to apply quality\n");
            return 0;
        }
        printf("Quality -> %d\n", q);
    } else if (strcmp(argv[1], "duration") == 0) {
        app_video_config_t *cfg = app_video_get_config_mutable();
        int d = atoi(argv[2]);
        if (d <= 0 || d > 3600) {
            printf("duration must be 1..3600 seconds\n");
            return 0;
        }
        cfg->duration_sec = (uint32_t)d;
        printf("Duration -> %d s\n", d);
    } else if (strcmp(argv[1], "fps") == 0) {
        app_video_config_t *cfg = app_video_get_config_mutable();
        int fps = atoi(argv[2]);
        if (fps < 0 || fps > 30) {
            printf("fps must be 0..30 (0 = uncapped max)\n");
            return 0;
        }
        cfg->frame_interval_ms = (fps == 0) ? 0U : (1000U / (uint32_t)fps);
        if (fps == 0) {
            printf("FPS cap -> off (max)\n");
        } else {
            printf("FPS cap -> ~%d fps (interval %" PRIu32 " ms)\n",
                   fps, cfg->frame_interval_ms);
        }
    } else {
        printf("Unknown parameter '%s'. Run `set` with no args for usage.\n",
               argv[1]);
    }
    return 0;
}

esp_err_t app_cli_init(void)
{
    if (s_repl) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "esp32> ";
    repl_config.max_cmdline_length = 64;
    repl_config.task_stack_size = 8192;

    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_err_t ret = esp_console_new_repl_uart(&uart_config, &repl_config, &s_repl);
    if (ret != ESP_OK) {
        return ret;
    }

    esp_console_cmd_t cmd = {
        .command = "record",
#if BOARD_HAS_PDM_MIC
        .help = "Start recording video and audio",
#else
        .help = "Start recording video to SD",
#endif
        .hint = NULL,
        .func = cmd_record,
    };
    ret = esp_console_cmd_register(&cmd);
    if (ret != ESP_OK) return ret;

    cmd.command = "photo";
    cmd.help = "Capture one max-resolution JPEG photo to SD";
    cmd.hint = NULL;
    cmd.func = cmd_photo;
    ret = esp_console_cmd_register(&cmd);
    if (ret != ESP_OK) return ret;

    cmd.command = "transfer";
    cmd.help = "Transfer a file in hex format";
    cmd.hint = "<filename>";
    cmd.func = cmd_transfer;
    ret = esp_console_cmd_register(&cmd);
    if (ret != ESP_OK) return ret;

    cmd.command = "ls";
    cmd.help = "List files on SD card";
    cmd.hint = NULL;
    cmd.func = cmd_ls;
    ret = esp_console_cmd_register(&cmd);
    if (ret != ESP_OK) return ret;

    cmd.command = "show";
    cmd.help = "Show current video configuration";
    cmd.hint = NULL;
    cmd.func = cmd_show;
    ret = esp_console_cmd_register(&cmd);
    if (ret != ESP_OK) return ret;

    cmd.command = "set";
    cmd.help = "Set a video parameter (run `set` with no args for usage)";
    cmd.hint = "<param> <value>";
    cmd.func = cmd_set;
    ret = esp_console_cmd_register(&cmd);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

esp_err_t app_cli_start(void)
{
    if (!s_repl) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_console_start_repl(s_repl);
}
