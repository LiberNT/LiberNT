# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     FsRtl name matcher host-test build configuration
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

add_executable(fsrtl_expression
    t_fsrtlexpr.c
    ${REACTOS_HOST_SOURCE_ROOT}/ntos/fsrtl/unicodename.c
    ${REACTOS_HOST_SOURCE_ROOT}/ntos/fsrtl/ansiname.c)
target_include_directories(fsrtl_expression PRIVATE
    include
    ${REACTOS_HOST_SOURCE_ROOT}/modules/rostests/kmtests/ntos_fsrtl)
target_compile_features(fsrtl_expression PRIVATE c_std_11)
target_compile_options(fsrtl_expression PRIVATE -fshort-wchar -Wall -Werror -Wno-unused-function -g
    -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(fsrtl_expression PRIVATE -fsanitize=address,undefined)
add_test(NAME fsrtl_expression COMMAND fsrtl_expression)
set_tests_properties(fsrtl_expression PROPERTIES TIMEOUT 20)
