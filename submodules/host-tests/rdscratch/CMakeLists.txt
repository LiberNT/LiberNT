# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Boot RAM volume write space host-test build configuration
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

set(RDSCRATCH_DIR ${REACTOS_HOST_SOURCE_ROOT}/drivers/storage/rdscratch)
set(EXPAND_DIR ${REACTOS_HOST_SOURCE_ROOT}/base/applications/cmdutils/lntexpandstorage)

add_executable(rdscratch_core t_rdscratch.c ${RDSCRATCH_DIR}/rdscore.c ${EXPAND_DIR}/expandcore.c)
target_include_directories(rdscratch_core PRIVATE ${RDSCRATCH_DIR} ${EXPAND_DIR})
set_source_files_properties(${EXPAND_DIR}/expandcore.c PROPERTIES COMPILE_DEFINITIONS EXPAND_VOLUME_ONLY)
target_compile_features(rdscratch_core PRIVATE c_std_11)
target_compile_options(rdscratch_core PRIVATE -Wall -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(rdscratch_core PRIVATE -fsanitize=address,undefined)

add_test(NAME rdscratch.store COMMAND rdscratch_core)
set_tests_properties(rdscratch.store PROPERTIES TIMEOUT 20)

if(TARGET ntfswim_ntfsimg)
    add_test(NAME rdscratch.grow_volume
             COMMAND /bin/sh ${CMAKE_CURRENT_SOURCE_DIR}/grow-test.sh $<TARGET_FILE:rdscratch_core> $<TARGET_FILE:ntfswim_ntfsimg>)
    set_tests_properties(rdscratch.grow_volume PROPERTIES TIMEOUT 120)
endif()
