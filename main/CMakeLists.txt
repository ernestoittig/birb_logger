idf_component_register(SRCS "birb_logger_main.c" "display.c" "wifi.c" "send_audio.c" "audio.c"
                       PRIV_REQUIRES spi_flash esp_lcd esp_driver_spi esp_driver_i2s esp_driver_gpio nvs_flash esp_wifi
                       INCLUDE_DIRS ".")
