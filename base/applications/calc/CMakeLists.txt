# PROJECT:     LiberNT Calculator
# LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
# PURPOSE:     Build the calculator
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

add_rc_deps(calc.rc
    ${CMAKE_CURRENT_SOURCE_DIR}/calcres.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lang/en-US.rc
    ${CMAKE_CURRENT_SOURCE_DIR}/res/calc.ico
    ${CMAKE_CURRENT_SOURCE_DIR}/res/calc.exe.manifest)
add_executable(calc
    number.c
    engine.c
    main.cpp
    calc.rc)
set_property(TARGET calc PROPERTY CXX_STANDARD 17)
set_property(TARGET calc PROPERTY CXX_STANDARD_REQUIRED ON)
set_module_type(calc win32gui UNICODE)
target_link_libraries(calc shellframe cpprt)
add_importlibs(calc user32 gdi32 gdiplus shell32 comctl32 dwmapi uxtheme advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET calc DESTINATION reactos/system32 FOR all)
