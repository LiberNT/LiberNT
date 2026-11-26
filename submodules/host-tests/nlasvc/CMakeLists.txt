# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Network Location Awareness Internet probe host test
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

add_executable(nlasvc_probe
    test.c
    ${REACTOS_HOST_SOURCE_ROOT}/base/services/nlasvc/probe.c)
target_include_directories(nlasvc_probe PRIVATE
    ${REACTOS_HOST_SOURCE_ROOT}/base/services/nlasvc
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/include)
target_compile_options(nlasvc_probe PRIVATE -Wall -Wextra)
add_test(NAME nlasvc.probe COMMAND nlasvc_probe)
