set(srcs "src/sys_init.c"
         "src/sys_com.c"
         "src/sys_event.c"
         "src/sys_hw.c"
         )

set(public_include "inc")

set(requires film_hal film_service film_app esp_hw_support spi_flash esp_psram)

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${public_include}
                       REQUIRES ${requires}
                      )
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u g_sys_hw_descriptor")
