# 查找所有子目录的src/*.c文件
file(GLOB component_srcs 
    "app/src/*.c"
)

# 自动包含所有子目录的inc路径
file(GLOB component_incs 
    "app/inc"
)

set(requires film_sys film_service)

idf_component_register(SRCS ${srcs} ${component_srcs}
                       INCLUDE_DIRS ${component_incs}
                       REQUIRES ${requires}
                      )