# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     ISO 9660 directory name host regression test
# COPYRIGHT:   Copyright 2026 Zhou Ji Yang <yinglanxing@gmail.com>

add_executable(freeldr_iso test.c
    ${REACTOS_HOST_SOURCE_ROOT}/boot/freeldr/freeldr/disk/isoname.c)
target_include_directories(freeldr_iso PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${REACTOS_HOST_SOURCE_ROOT}/boot/freeldr/freeldr/include)
target_compile_options(freeldr_iso PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/shim.h -Wall -Wextra)
add_test(NAME freeldr.iso_names COMMAND freeldr_iso)
