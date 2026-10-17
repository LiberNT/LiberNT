add_definitions(-D_NTSYSTEM_)

list(APPEND SOURCE
    arblib.c)

add_library(arblib ${SOURCE})
add_dependencies(arblib bugcodes xdk)
