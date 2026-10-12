
include_directories(BEFORE ${REACTOS_SOURCE_DIR}/sdk/include/wine)
add_definitions(-D__WINESRC__)

list(APPEND SOURCE
    conhost.c
    window.c)

add_rc_deps(conhost.rc ${CMAKE_CURRENT_SOURCE_DIR}/conhost.manifest)
add_executable(conhost ${SOURCE} conhost.rc)
set_module_type(conhost win32gui UNICODE)
target_link_libraries(conhost wine)
add_importlibs(conhost user32 gdi32 advapi32 comctl32 uxtheme msvcrt kernel32 ntdll)
add_cd_file(TARGET conhost DESTINATION reactos/system32 FOR all)
