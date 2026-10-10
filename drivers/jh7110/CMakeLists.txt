#
# PROJECT:     LiberNT StarFive JH7110 drivers
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build rules for the StarFive JH7110 board drivers
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

math(EXPR _JH7110_NT_NUM "${REACTOS_TARGET_NT} + 0")
if(REACTOS_USE_WDDM AND (REACTOS_WDDM_TARGET_LEVEL GREATER_EQUAL 2000))
    add_subdirectory(jh7110dc)
endif()
if(_JH7110_NT_NUM GREATER_EQUAL 1536)
    add_subdirectory(jh7110gmac)
endif()
add_subdirectory(jh7110pmic)
add_subdirectory(jh7110thermal)
add_subdirectory(jh7110wdt)
