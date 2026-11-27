
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

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -print-file-name=include
        OUTPUT_VARIABLE QUICKJS_GCC_INCLUDE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    target_compile_options(quickjs PRIVATE -idirafter ${QUICKJS_GCC_INCLUDE_DIR})
endif()
