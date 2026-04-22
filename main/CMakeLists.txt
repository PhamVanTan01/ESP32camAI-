idf_component_register(
    SRCS
        "ipc.c"
        "task_capture.c"
        "task_writer.c"
        "app_video.c"
        "app_cli.c"
        "app_events.c"
        "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        bsp
        device_hal
        avi_muxer
        utils
        driver
        esp_timer
        fatfs
        esp32-camera
        console
        nvs_flash
        vfs
        esp_event
        freertos
    PRIV_REQUIRES spi_flash esp_psram
)
