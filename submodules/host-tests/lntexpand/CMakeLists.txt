# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Storage expander host-test build configuration
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

add_executable(lntexpand_stale_gpt
    t_stalegpt.c
    ${REACTOS_HOST_SOURCE_ROOT}/base/applications/cmdutils/lntexpandstorage/expandcore.c)
target_include_directories(lntexpand_stale_gpt PRIVATE
    ${REACTOS_HOST_SOURCE_ROOT}/base/applications/cmdutils/lntexpandstorage)
target_compile_features(lntexpand_stale_gpt PRIVATE c_std_11)
target_compile_options(lntexpand_stale_gpt PRIVATE -Wall -Werror -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(lntexpand_stale_gpt PRIVATE -fsanitize=address,undefined)
add_test(NAME lntexpand.stale_gpt COMMAND lntexpand_stale_gpt)
set_tests_properties(lntexpand.stale_gpt PROPERTIES TIMEOUT 20)
