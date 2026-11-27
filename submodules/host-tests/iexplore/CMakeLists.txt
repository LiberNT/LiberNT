# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Web browser helper and plain page reader host tests
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

add_executable(iexplore_browser
    test.c
    ${REACTOS_HOST_SOURCE_ROOT}/base/applications/iexplore/browser.c)
target_include_directories(iexplore_browser PRIVATE ${REACTOS_HOST_SOURCE_ROOT}/base/applications/iexplore)
target_compile_options(iexplore_browser PRIVATE -Wall -Wextra)
add_test(NAME iexplore.browser COMMAND iexplore_browser)

add_executable(iexplore_html
    html_test.c
    ${REACTOS_HOST_SOURCE_ROOT}/base/applications/iexplore/html.c)
target_include_directories(iexplore_html PRIVATE ${REACTOS_HOST_SOURCE_ROOT}/base/applications/iexplore)
target_compile_options(iexplore_html PRIVATE -Wall -Wextra)
add_test(NAME iexplore.html COMMAND iexplore_html)
