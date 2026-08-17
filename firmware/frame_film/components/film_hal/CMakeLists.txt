set(srcs "src/hal_init.c"
         "src/hal_button.c"
         "src/hal_encoder.c"
         "src/hal_bat.c"
         "src/hal_led.c"
         "src/hal_sd.c"
         "src/hal_pwr.c"
         "src/hal_epd_360.c"
         "src/hal_epd_368.c"
         "src/hal_epd_709.c"
         )

set(public_include "inc")

set(requires film_sys led_strip driver fatfs esp_adc encoder button)

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${public_include}
                       REQUIRES ${requires}
                      )