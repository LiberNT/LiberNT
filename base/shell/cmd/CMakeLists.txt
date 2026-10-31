
include_directories(BEFORE ${REACTOS_SOURCE_DIR}/sdk/include/wine)
add_definitions(-D__WINESRC__)

list(APPEND SOURCE
    batch.c
    builtins.c
    directory.c
    wcmdmain.c)

add_rc_deps(cmd.rc ${CMAKE_CURRENT_SOURCE_DIR}/res/terminal.ico)
add_executable(cmd ${SOURCE} cmd.rc)
set_module_type(cmd win32cui UNICODE)
target_link_libraries(cmd wine)
add_importlibs(cmd shell32 user32 advapi32 kernelbase msvcrt kernel32 ntdll)
add_cd_file(TARGET cmd DESTINATION reactos/system32 FOR all)
