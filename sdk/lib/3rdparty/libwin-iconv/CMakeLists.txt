
if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

add_library(iconv-static win_iconv.c)
set_target_properties(iconv-static PROPERTIES OUTPUT_NAME "iconv")
add_dependencies(iconv-static psdk)

if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(iconv-static PRIVATE -Wno-unused-but-set-global)
endif()
