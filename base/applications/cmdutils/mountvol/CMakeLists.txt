# PROJECT:     LiberNT Mount Volume Command
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build script for mountvol.exe
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

add_executable(mountvol mountvol.c mountvol.rc)
set_module_type(mountvol win32cui UNICODE)
target_link_libraries(mountvol conutils)
add_importlibs(mountvol msvcrt kernel32)
add_cd_file(TARGET mountvol DESTINATION reactos/system32 FOR all)
