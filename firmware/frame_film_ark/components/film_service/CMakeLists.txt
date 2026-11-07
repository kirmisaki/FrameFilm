set(srcs "src/service_init.c"
         "src/service_ble.c"
         "src/service_ble_gatts.c"
         "src/service_ota.c"
         "src/service_param.c"
         "src/service_time.c"
         "src/service_monitor.c"
         "src/service_file.c"
         "src/service_film.c"
         "src/service_wifi.c"
         )

set(public_include "inc")

set(requires film_hal film_sys  bt nvs_flash app_update esp_wifi esp_netif esp_http_client json mbedtls)

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${public_include}
                       REQUIRES ${requires}
                      )

option(ARK_TRANSFER_BENCH "Temporary Ark HTTP sink and SD benchmark" OFF)
if(ARK_TRANSFER_BENCH)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ARK_TRANSFER_BENCH=1)
endif()

if(ARK_AP_FILE_BENCH)
    if(ARK_TRANSFER_BENCH)
        message(FATAL_ERROR "ARK_AP_FILE_BENCH requires the real SD pipeline, not ARK_TRANSFER_BENCH")
    endif()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ARK_AP_FILE_BENCH=1)
    # A local build-only credential; never include it in compile definitions or logs.
    set(bench_secret "${CMAKE_CURRENT_BINARY_DIR}/ap_file_bench_secret.h")
    if(NOT EXISTS "${bench_secret}")
        string(RANDOM LENGTH 24 ALPHABET "0123456789ABCDEF" bench_password)
        file(WRITE "${bench_secret}" "#pragma once\n#define ARK_AP_FILE_BENCH_PASSWORD \"${bench_password}\"\n")
    endif()
    target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endif()
