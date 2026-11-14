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

function(rtl_srw_variant NAME)
    add_executable(${NAME} t_srw.c ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/rtl/srw.c)
    target_include_directories(${NAME} PRIVATE srw)
    target_compile_features(${NAME} PRIVATE c_std_11)
    target_compile_options(${NAME} PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter -g)
    target_link_libraries(${NAME} PRIVATE pthread)
    add_test(NAME ${NAME} COMMAND ${NAME})
    set_tests_properties(${NAME} PROPERTIES TIMEOUT 60)
endfunction()

rtl_srw_variant(rtl_srw)
target_compile_options(rtl_srw PRIVATE -O2)

rtl_srw_variant(rtl_srw_tsan)
target_compile_options(rtl_srw_tsan PRIVATE -O1 -fsanitize=thread -fno-omit-frame-pointer)
target_link_options(rtl_srw_tsan PRIVATE -fsanitize=thread)

function(rtl_critsec_variant NAME)
    add_executable(${NAME} t_critsec.c ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/rtl/critical.c)
    target_include_directories(${NAME} PRIVATE critsec)
    target_compile_features(${NAME} PRIVATE c_std_11)
    target_compile_options(${NAME} PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter -g)
    target_link_libraries(${NAME} PRIVATE pthread)
    add_test(NAME ${NAME} COMMAND ${NAME})
    set_tests_properties(${NAME} PROPERTIES TIMEOUT 60)
endfunction()

rtl_critsec_variant(rtl_critsec)
target_compile_options(rtl_critsec PRIVATE -O2)

rtl_critsec_variant(rtl_critsec_tsan)
target_compile_options(rtl_critsec_tsan PRIVATE -O1 -fsanitize=thread -fno-omit-frame-pointer)
target_link_options(rtl_critsec_tsan PRIVATE -fsanitize=thread)

function(rtl_pushlock_variant NAME)
    add_executable(${NAME} t_pushlock.c ${REACTOS_HOST_SOURCE_ROOT}/ntos/ex/expushlock.c)
    target_include_directories(${NAME} PRIVATE pushlock)
    target_compile_features(${NAME} PRIVATE c_std_11)
    target_compile_options(${NAME} PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter -g)
    target_link_libraries(${NAME} PRIVATE pthread)
    add_test(NAME ${NAME} COMMAND ${NAME})
    set_tests_properties(${NAME} PROPERTIES TIMEOUT 60)
endfunction()

rtl_pushlock_variant(rtl_pushlock)
target_compile_options(rtl_pushlock PRIVATE -O2)

rtl_pushlock_variant(rtl_pushlock_tsan)
target_compile_options(rtl_pushlock_tsan PRIVATE -O1 -fno-inline -fsanitize=thread -fno-omit-frame-pointer)
target_link_options(rtl_pushlock_tsan PRIVATE -fsanitize=thread)
