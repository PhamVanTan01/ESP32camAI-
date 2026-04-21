idf_component_register(
    SRCS "camera_hal.c" "sdcard_hal.c" "fs_hal.c" "audio_hal.c"
    INCLUDE_DIRS "."
    REQUIRES bsp driver fatfs esp32-camera vfs sdmmc
    PRIV_REQUIRES spi_flash
)
