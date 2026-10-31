include("${CMAKE_CURRENT_LIST_DIR}/../../c3_component.cmake")
idf_component_register(SRCS "${CLASSIC_ROOT}/components/film_sys/src/sys_init.c"
  "${CLASSIC_ROOT}/components/film_sys/src/sys_com.c"
  INCLUDE_DIRS "${CLASSIC_ROOT}/components/film_sys/inc"
  REQUIRES film_hal film_service)
c3_board_config()
