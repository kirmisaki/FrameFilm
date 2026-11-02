include("${CMAKE_CURRENT_LIST_DIR}/../../c3_component.cmake")
idf_component_register(SRCS "c3_hal.c"
  "${CLASSIC_ROOT}/components/film_hal/src/hal_epd_398e4.c"
  INCLUDE_DIRS "inc" "${CLASSIC_ROOT}/components/film_hal/inc"
  REQUIRES film_sys driver esp_hw_support)
c3_board_config()
set_source_files_properties("${CLASSIC_ROOT}/components/film_hal/src/hal_epd_398e4.c"
  PROPERTIES COMPILE_DEFINITIONS "hal_epd_display_film=c3_raw_display_film;hal_epd_deinit=c3_raw_epd_deinit")
