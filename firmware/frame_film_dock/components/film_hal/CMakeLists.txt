set(srcs "src/hal_init.c"
         "src/hal_button.c"
         "src/hal_led.c"
         "src/hal_sd.c"
         "src/hal_flash.c"
         "src/hal_epd.c"
         "src/hal_audio.c"
         "src/hal_usb.c"
         )

set(public_include "inc")

set(requires film_sys led_strip driver fatfs spiffs button esp_driver_i2c esp_driver_i2s esp_codec_dev usb_device_uac)

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${public_include}
                       REQUIRES ${requires}
                      )