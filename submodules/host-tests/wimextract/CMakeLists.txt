# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     WIM image extraction host-test build configuration
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

set(LIBWIM_DIR ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/libwim)
set(LIBWIM_INCLUDE ${REACTOS_HOST_SOURCE_ROOT}/sdk/include/libernt/libs/libwim)

add_executable(wimextract_extract
    t_wimextract.c
    ${LIBWIM_DIR}/lzx.c
    ${LIBWIM_DIR}/sha1.c
    ${LIBWIM_DIR}/wimboot.c
    ${LIBWIM_DIR}/wimcore.c
    ${LIBWIM_DIR}/xpress_huff.c)
target_compile_features(wimextract_extract PRIVATE c_std_11)
target_compile_definitions(wimextract_extract PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE _DARWIN_C_SOURCE)
target_compile_options(wimextract_extract PRIVATE -g -O1 -fsanitize=address -fno-omit-frame-pointer)
target_link_options(wimextract_extract PRIVATE -fsanitize=address)
target_include_directories(wimextract_extract PRIVATE ${LIBWIM_INCLUDE})

add_executable(wimextract_wimage
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/tools/wimage/wimage.c
    ${LIBWIM_DIR}/lzx.c
    ${LIBWIM_DIR}/sha1.c
    ${LIBWIM_DIR}/wimcore.c
    ${LIBWIM_DIR}/xpress_huff.c
    ${LIBWIM_DIR}/wim_capture.c
    ${LIBWIM_DIR}/wim_io.c
    ${LIBWIM_DIR}/wim_read.c
    ${LIBWIM_DIR}/wim_write.c)
target_compile_features(wimextract_wimage PRIVATE c_std_11)
target_compile_definitions(wimextract_wimage PRIVATE
    WIMAGE_VERSION_STRING="1.0-test" _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE _DARWIN_C_SOURCE)
target_include_directories(wimextract_wimage PRIVATE ${LIBWIM_INCLUDE} ${REACTOS_HOST_SOURCE_ROOT}/sdk/include/host)
find_package(Threads)
if(Threads_FOUND)
    target_link_libraries(wimextract_wimage PRIVATE Threads::Threads)
endif()

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME wimextract.images COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/wim-extract-test.py
        --extract $<TARGET_FILE:wimextract_extract> --wimage $<TARGET_FILE:wimextract_wimage>)
    set_tests_properties(wimextract.images PROPERTIES TIMEOUT 120)
endif()
