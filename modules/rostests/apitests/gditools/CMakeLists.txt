
include_directories(${REACTOS_SOURCE_DIR}/subsystems/win/win32k/include)

add_library(gditools
    gditools.c)

add_dependencies(gditools xdk)
