# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     ACPI PCI interrupt routing host regression test
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

add_executable(acpi_pci_routing test.c
    ${REACTOS_HOST_SOURCE_ROOT}/drivers/bus/acpi/busmgr/acpi_pci_routing.c)
target_include_directories(acpi_pci_routing BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${REACTOS_HOST_SOURCE_ROOT}/drivers/bus/acpi/acpica/include)
target_compile_options(acpi_pci_routing PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-multichar)
add_test(NAME acpi.pci_bridge_prt COMMAND acpi_pci_routing)
