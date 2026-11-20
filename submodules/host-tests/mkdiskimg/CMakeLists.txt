# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Disk image creator host-test build configuration
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

add_executable(mkdiskimg_tool ${REACTOS_HOST_SOURCE_ROOT}/sdk/tools/mkdiskimg/mkdiskimg.c)
target_compile_options(mkdiskimg_tool PRIVATE -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(mkdiskimg_tool PRIVATE -fsanitize=address,undefined)

add_executable(mkdiskimg_hybrid_cd t_hybridcd.c)
target_compile_features(mkdiskimg_hybrid_cd PRIVATE c_std_11)
target_compile_options(mkdiskimg_hybrid_cd PRIVATE -Wall -Werror -g -O1)
add_test(NAME mkdiskimg.hybrid_cd
         COMMAND mkdiskimg_hybrid_cd $<TARGET_FILE:mkdiskimg_tool> ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(mkdiskimg.hybrid_cd PROPERTIES TIMEOUT 20)
