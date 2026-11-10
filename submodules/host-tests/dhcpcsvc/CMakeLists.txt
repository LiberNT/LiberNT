# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     DHCP client retransmission schedule host regression test
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>

add_executable(dhcpcsvc_retransmit t_retransmit.c
    ${REACTOS_HOST_SOURCE_ROOT}/base/services/dhcpcsvc/dhcp/retransmit.c)
target_include_directories(dhcpcsvc_retransmit PRIVATE
    ${REACTOS_HOST_SOURCE_ROOT}/base/services/dhcpcsvc/include)
target_compile_options(dhcpcsvc_retransmit PRIVATE -Wall -Wextra)
add_test(NAME dhcpcsvc.retransmit COMMAND dhcpcsvc_retransmit)
