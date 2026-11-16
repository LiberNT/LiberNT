include_directories(
    include
    ${REACTOS_SOURCE_DIR}/subsystems/win/win32k/include)

add_library(user32_wine_modern
    wine/input.c
    wine/misc.c
    wine/sysparams.c
    wine/win.c)
if(NOT ARM64EC_RUNTIME)
    target_link_libraries(user32_wine_modern wine)
else()
    target_link_libraries(user32_wine_modern wine win32ksys)
endif()
add_dependencies(user32_wine_modern wine psdk)
if(MSVC)
    target_compile_options(user32_wine_modern PRIVATE /wd4312)
endif()

add_library(user32_modern dpi.c pointer.c)
if(ARM64EC_RUNTIME)
    target_link_libraries(user32_modern win32ksys)
endif()
add_dependencies(user32_modern psdk)
