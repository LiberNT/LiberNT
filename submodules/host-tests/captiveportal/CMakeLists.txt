# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Captive portal sign-in page reader and form encoder host test
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

add_executable(captiveportal_html
    test.c
    ${REACTOS_HOST_SOURCE_ROOT}/base/applications/network/captiveportal/html.c)
target_include_directories(captiveportal_html PRIVATE
    ${REACTOS_HOST_SOURCE_ROOT}/base/applications/network/captiveportal)
target_compile_options(captiveportal_html PRIVATE -Wall -Wextra)
add_test(NAME captiveportal.html COMMAND captiveportal_html)
