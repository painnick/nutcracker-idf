set(srcs
    rccar.c
    rccar_drive.c
    rccar_motor.c
    rccar_humidifier.c
    rccar_neopixel.c
    rccar_ws2812_encoder.c
    rccar_dfplayer.c
    rccar_storage.c
)

idf_component_register(SRCS ${srcs}
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_rmt nvs_flash esp_timer log freertos)
