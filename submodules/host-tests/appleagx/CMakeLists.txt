# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Apple graphics firmware data host regression test
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

add_executable(appleagx_fw test.c ${REACTOS_HOST_SOURCE_ROOT}/sdk/lib/drivers/applertkit/agxfw.c)
target_include_directories(appleagx_fw PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/include)
target_compile_options(appleagx_fw PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/shim.h -Wall -Wextra)
add_test(NAME appleagx.fw COMMAND appleagx_fw)
