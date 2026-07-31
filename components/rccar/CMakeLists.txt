set(srcs
    rccar.c
    rccar_drive.c
    rccar_motor.c
    rccar_servo.c
    rccar_shiftreg.c
    rccar_led.c
    rccar_dfplayer.c
    rccar_storage.c
)

idf_component_register(SRCS ${srcs}
    INCLUDE_DIRS "."
    REQUIRES driver nvs_flash esp_timer log freertos)
