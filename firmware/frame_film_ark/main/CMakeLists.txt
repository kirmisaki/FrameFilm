idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS ".")
if(ARK_AP_FILE_BENCH)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ARK_AP_FILE_BENCH=1)
endif()
