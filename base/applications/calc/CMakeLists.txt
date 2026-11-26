# PROJECT:     LiberNT Calculator
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the calculator
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

add_rc_deps(calc.rc
    ${CMAKE_CURRENT_SOURCE_DIR}/calcres.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lang/en-US.rc
    ${CMAKE_CURRENT_SOURCE_DIR}/res/calc.ico)
add_executable(calc
    number.c
    engine.c
    main.c
    calc.rc)
set_module_type(calc win32gui UNICODE)
add_importlibs(calc uxtheme comctl32 shell32 advapi32 user32 gdi32 msvcrt kernel32)
add_cd_file(TARGET calc DESTINATION reactos/system32 FOR all)
