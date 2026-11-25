# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     WIM backed NTFS volume host-test build configuration
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

enable_language(CXX)

set(NTFSLIB_ROOT ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/fslib/ntfslib_new)
set(LIBWIM_DIR ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/libwim)
set(LIBWIM_INCLUDE ${REACTOS_HOST_SOURCE_ROOT}/sdk/include/libernt/libs/libwim)
set(EXPAND_DIR ${REACTOS_HOST_SOURCE_ROOT}/base/applications/cmdutils/lntexpandstorage)

file(GLOB_RECURSE NTFSWIM_LIBRARY ${NTFSLIB_ROOT}/src/*.cpp)
list(REMOVE_ITEM NTFSWIM_LIBRARY ${NTFSLIB_ROOT}/src/btree/directory/dbg.cpp)

add_executable(ntfswim_ntfsimg
    ${NTFSWIM_LIBRARY}
    ${NTFSLIB_ROOT}/env/linux/linux.cpp
    ${NTFSLIB_ROOT}/linux/fuse/ntfslib_fuse.cpp
    ${LIBWIM_DIR}/lzx.c
    ${LIBWIM_DIR}/sha1.c
    ${LIBWIM_DIR}/wimboot.c
    ${LIBWIM_DIR}/wimcore.c
    ${LIBWIM_DIR}/xpress_huff.c
    ${EXPAND_DIR}/expandcore.c)
target_compile_features(ntfswim_ntfsimg PRIVATE cxx_std_17)
target_compile_definitions(ntfswim_ntfsimg PRIVATE NTFSLIB_PORTABLE=1 _FILE_OFFSET_BITS=64 NTFSLIB_HAVE_FUSE=0)
set_source_files_properties(${EXPAND_DIR}/expandcore.c PROPERTIES COMPILE_DEFINITIONS EXPAND_VOLUME_ONLY)
target_compile_options(ntfswim_ntfsimg PRIVATE
    -fshort-wchar -Wno-multichar -g -O1 -fsanitize=address -fno-omit-frame-pointer)
target_link_options(ntfswim_ntfsimg PRIVATE -fsanitize=address)
target_include_directories(ntfswim_ntfsimg PRIVATE
    ${NTFSLIB_ROOT}/include/public
    ${NTFSLIB_ROOT}/include/internal
    ${NTFSLIB_ROOT}/env/linux
    ${NTFSLIB_ROOT}/src/format
    ${LIBWIM_INCLUDE}
    ${EXPAND_DIR})

add_executable(ntfswim_wimage
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/tools/wimage/wimage.c
    ${LIBWIM_DIR}/lzx.c
    ${LIBWIM_DIR}/sha1.c
    ${LIBWIM_DIR}/wimcore.c
    ${LIBWIM_DIR}/xpress_huff.c
    ${LIBWIM_DIR}/wim_capture.c
    ${LIBWIM_DIR}/wim_io.c
    ${LIBWIM_DIR}/wim_read.c
    ${LIBWIM_DIR}/wim_write.c)
target_compile_features(ntfswim_wimage PRIVATE c_std_11)
target_compile_definitions(ntfswim_wimage PRIVATE
    WIMAGE_VERSION_STRING="1.0-test" _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE _DARWIN_C_SOURCE)
target_include_directories(ntfswim_wimage PRIVATE ${LIBWIM_INCLUDE} ${REACTOS_HOST_SOURCE_ROOT}/sdk/include/host)
find_package(Threads)
if(Threads_FOUND)
    target_link_libraries(ntfswim_wimage PRIVATE Threads::Threads)
endif()

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME ntfswim.pointer_files COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/wim-volume-test.py
        --ntfsimg $<TARGET_FILE:ntfswim_ntfsimg> --wimage $<TARGET_FILE:ntfswim_wimage>)
    set_tests_properties(ntfswim.pointer_files PROPERTIES TIMEOUT 120)
endif()
