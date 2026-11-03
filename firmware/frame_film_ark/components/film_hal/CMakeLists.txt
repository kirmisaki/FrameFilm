set(srcs "src/hal_init.c"
         "src/hal_hardware.c"
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

if(CONFIG_ARK_EPD_PROFILE)
    list(APPEND requires esp_timer)
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${public_include}
                       REQUIRES ${requires}
                      )
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u ark_hardware_descriptor")
if(ARK_AP_FILE_BENCH)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ARK_AP_FILE_BENCH=1)
endif()
