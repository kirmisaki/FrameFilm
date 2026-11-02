# Only the C3 target includes this helper. Existing targets are untouched.
get_filename_component(C3_ROOT "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(CLASSIC_ROOT "${C3_ROOT}/../frame_film" ABSOLUTE)
function(c3_board_config)
  target_compile_options(${COMPONENT_LIB} PRIVATE
    -include "${C3_ROOT}/components/film_hal/inc/c3_board.h"
    -Wno-error=format)
endfunction()
