set(srcs "src/sys_init.c"
         )

set(public_include "inc")

set(requires film_hal film_service film_app)

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${public_include}
                       REQUIRES ${requires}
                      )