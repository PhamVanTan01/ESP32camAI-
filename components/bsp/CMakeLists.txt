idf_component_register(
    SRCS "bsp_board.c" "bsp_btn.c"
    INCLUDE_DIRS "include"
    REQUIRES driver nvs_flash esp_psram esp_system spi_flash heap esp_timer
)
