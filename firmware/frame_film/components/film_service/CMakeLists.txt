set(srcs "src/service_init.c"
         "src/service_ble.c"
         "src/service_ble_gatts.c"
         "src/service_ota.c"
         "src/service_image.c"
         "src/service_param.c"
         "src/service_monitor.c"
         "src/service_file.c"
         )

set(public_include "inc")

set(requires film_hal film_sys  bt nvs_flash)

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${public_include}
                       REQUIRES ${requires}
                      )