# Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(hostbind_dll MODULE hostbind_dll.c)
set_module_type(hostbind_dll win32dll HOSTBOUND)
add_importlibs(hostbind_dll msvcrt kernel32)
add_rostests_file(TARGET hostbind_dll)

add_library(hostbind_entry MODULE hostbind_entry.c)
set_module_type(hostbind_entry win32dll ENTRYPOINT DllMain 12 HOSTBOUND)
add_importlibs(hostbind_entry kernel32)
add_rostests_file(TARGET hostbind_entry)

add_library(hostbind_noentry MODULE hostbind_entry.c)
set_module_type(hostbind_noentry win32dll ENTRYPOINT 0 HOSTBOUND)
add_importlibs(hostbind_noentry kernel32)
add_rostests_file(TARGET hostbind_noentry)

add_executable(hostbind_child hostbind_child.c)
set_module_type(hostbind_child win32cui HOSTBOUND)
add_importlibs(hostbind_child msvcrt kernel32)
add_rostests_file(TARGET hostbind_child)

add_executable(hostbind_apitest HostBinding.c testlist.c)
target_link_libraries(hostbind_apitest wine)
set_module_type(hostbind_apitest win32cui)
add_importlibs(hostbind_apitest msvcrt kernel32 ntdll)
add_dependencies(hostbind_apitest hostbind_dll hostbind_entry hostbind_noentry hostbind_child)
add_rostests_file(TARGET hostbind_apitest)
