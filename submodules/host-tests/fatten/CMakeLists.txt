# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     FAT image creator host-test build configuration
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

set(FATTEN_SOURCE_DIR ${REACTOS_HOST_SOURCE_ROOT}/sdk/tools/fatten)
set(FATTEN_FATFS_DIR ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/3rdparty/fatfs)

add_executable(fatten_tool
    ${FATTEN_SOURCE_DIR}/fatten.c
    ${FATTEN_SOURCE_DIR}/diskio.c
    ${FATTEN_FATFS_DIR}/ff.c
    ${FATTEN_FATFS_DIR}/ffunicode.c)
target_include_directories(fatten_tool PRIVATE ${FATTEN_SOURCE_DIR} ${FATTEN_FATFS_DIR})
target_compile_definitions(fatten_tool PRIVATE __REACTOS__ _FILE_OFFSET_BITS=64 _POSIX_C_SOURCE=200809L)
target_compile_options(fatten_tool PRIVATE -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(fatten_tool PRIVATE -fsanitize=address,undefined)

add_executable(fatten_fstype t_fstype.c)
target_compile_features(fatten_fstype PRIVATE c_std_11)
target_compile_options(fatten_fstype PRIVATE -Wall -Werror -g -O1)
add_test(NAME fatten.fstype
         COMMAND fatten_fstype $<TARGET_FILE:fatten_tool> ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(fatten.fstype PROPERTIES TIMEOUT 20)
