set(srcs "src/hal_init.c"
         "src/hal_button.c"
         "src/hal_bat.c"
         "src/hal_led.c"
         "src/hal_sd.c"
         "src/hal_pwr.c"
         "src/hal_wifi.c"
         "src/hal_epd_370.c"
         )

set(public_include "inc")

set(requires film_sys led_strip driver fatfs esp_adc button esp_wifi esp_netif esp_event esp_hw_support spi_flash esp_psram)

if(CONFIG_ARK_HAS_NFC)
    list(APPEND srcs "src/hal_nfc.c")
    list(APPEND requires st25r3916_lib)
    list(APPEND public_include "../st25r3916_lib/include")
endif()

if(CONFIG_ARK_EPD_PROFILE)
    list(APPEND requires esp_timer)
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${public_include}
                       REQUIRES ${requires}
                      )
