idf_component_register(
    SRCS "avi_muxer.c"
    INCLUDE_DIRS "."
    REQUIRES esp_common esp_timer esp_psram
)
