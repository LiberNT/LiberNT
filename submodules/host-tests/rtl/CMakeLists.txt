# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Kernel RTL host-test build configuration
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

function(rtl_rbtree_variant NAME)
    add_executable(${NAME} t_rbtree.c ${REACTOS_HOST_SOURCE_ROOT}/ntos/rtl/rbtree.c)
    target_include_directories(${NAME} PRIVATE include)
    target_compile_features(${NAME} PRIVATE c_std_11)
    target_compile_options(${NAME} PRIVATE -Wall -Wextra -Werror -g)
    add_test(NAME ${NAME} COMMAND ${NAME})
    set_tests_properties(${NAME} PROPERTIES TIMEOUT 20)
endfunction()

rtl_rbtree_variant(rtl_rbtree)
target_compile_options(rtl_rbtree PRIVATE -O2)

rtl_rbtree_variant(rtl_rbtree_asan)
target_compile_options(rtl_rbtree_asan PRIVATE -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(rtl_rbtree_asan PRIVATE -fsanitize=address,undefined)
