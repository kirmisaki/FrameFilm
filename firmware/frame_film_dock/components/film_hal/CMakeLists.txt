set(srcs "src/hal_init.c"
         "src/hal_button.c"
         "src/hal_led.c"
         "src/hal_sd.c"
         "src/hal_epd.c"
         )

set(public_include "inc")

set(requires film_sys led_strip driver fatfs button esp_driver_i2c)

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${public_include}
                       REQUIRES ${requires}
                      )