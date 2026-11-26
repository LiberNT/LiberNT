# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Calculator arithmetic and key handling host test
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

add_executable(calc_engine
    test.c
    ${REACTOS_HOST_SOURCE_ROOT}/base/applications/calc/number.c
    ${REACTOS_HOST_SOURCE_ROOT}/base/applications/calc/engine.c)
target_include_directories(calc_engine PRIVATE ${REACTOS_HOST_SOURCE_ROOT}/base/applications/calc)
target_compile_options(calc_engine PRIVATE -Wall -Wextra)
target_link_libraries(calc_engine PRIVATE m)
add_test(NAME calc.engine COMMAND calc_engine)
