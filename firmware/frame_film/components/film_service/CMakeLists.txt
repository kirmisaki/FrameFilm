set(srcs "src/service_init.c"
         "src/service_ble.c"
         "src/service_ble_gatts.c"
         "src/service_ota.c"
         "src/service_param.c"
         "src/service_monitor.c"
         "src/service_file.c"
         "src/service_film.c"
         "src/service_wifi.c"
         )

set(public_include "inc")

set(requires film_hal film_sys  bt nvs_flash app_update esp_wifi esp_netif esp_http_client)

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${public_include}
                       REQUIRES ${requires}
                      )