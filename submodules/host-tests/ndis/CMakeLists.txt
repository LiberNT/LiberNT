# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     NDIS WDI host regression tests
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>

add_executable(ndis_rnr t_rnr.c)
target_include_directories(ndis_rnr PRIVATE ${REACTOS_HOST_SOURCE_ROOT}/drivers/network/ndis/include)
target_compile_options(ndis_rnr PRIVATE -Wall -Wextra)
add_test(NAME ndis.rnr COMMAND ndis_rnr)
