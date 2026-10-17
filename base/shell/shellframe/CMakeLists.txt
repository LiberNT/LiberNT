
file(GLOB shellframe_icons ${CMAKE_CURRENT_SOURCE_DIR}/res/icons/*.svg)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shellframe_icons.cpp
    COMMAND ${CMAKE_COMMAND} -DICON_DIR=${CMAKE_CURRENT_SOURCE_DIR}/res/icons
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/shellframe_icons.cpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_icons.cmake
    DEPENDS ${shellframe_icons} ${CMAKE_CURRENT_SOURCE_DIR}/embed_icons.cmake)

list(APPEND SOURCE
    canvas.cpp
    icons.cpp
    navigation.cpp
    theme.cpp
    window.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/shellframe_icons.cpp)

add_library(shellframe STATIC ${SOURCE})
target_include_directories(shellframe PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(shellframe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(shellframe PRIVATE UNICODE _UNICODE)
target_link_libraries(shellframe PRIVATE libmsvcrt oldnames)
add_dependencies(shellframe psdk)
