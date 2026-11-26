
list(APPEND SOURCE
    dtoa.c
    libregexp.c
    libunicode.c
    quickjs.c)

add_library(quickjs STATIC ${SOURCE})
target_include_directories(quickjs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(quickjs PRIVATE _DLL __USE_CRTIMP _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
target_compile_options(quickjs PRIVATE -w -include ${CMAKE_CURRENT_SOURCE_DIR}/quickjs_libernt.h)
add_dependencies(quickjs psdk)
