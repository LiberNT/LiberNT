# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Web browser address, character set, title and generated page helper host test
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

add_executable(iexplore_browser
    test.c
    ${REACTOS_HOST_SOURCE_ROOT}/base/applications/iexplore/browser.c)
target_include_directories(iexplore_browser PRIVATE ${REACTOS_HOST_SOURCE_ROOT}/base/applications/iexplore)
target_compile_options(iexplore_browser PRIVATE -Wall -Wextra)
add_test(NAME iexplore.browser COMMAND iexplore_browser)
