set(srcs "main.c" "app_video.c" "app_lcd.c")

idf_component_register(SRCS "${srcs}"
                       PRIV_INCLUDE_DIRS .
                       PRIV_REQUIRES esp_pm esp_driver_spi)