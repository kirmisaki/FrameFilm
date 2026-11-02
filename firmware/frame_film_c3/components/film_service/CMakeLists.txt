include("${CMAKE_CURRENT_LIST_DIR}/../../c3_component.cmake")
set(srcs "service_file_c3.c")
foreach(name init ble ble_gatts ota param monitor film wifi)
  list(APPEND srcs "${CLASSIC_ROOT}/components/film_service/src/service_${name}.c")
endforeach()
idf_component_register(SRCS ${srcs}
  INCLUDE_DIRS "${CLASSIC_ROOT}/components/film_service/inc"
  REQUIRES film_hal film_sys bt nvs_flash app_update esp_wifi esp_netif esp_http_client json spi_flash)
c3_board_config()
