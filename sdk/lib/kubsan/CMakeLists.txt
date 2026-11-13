
add_library(kubsan STATIC kubsan.c)
add_dependencies(kubsan xdk bugcodes)

add_library(kubsan_ntos STATIC kubsan.c)
target_compile_definitions(kubsan_ntos PRIVATE _NTSYSTEM_)
add_dependencies(kubsan_ntos xdk bugcodes)
