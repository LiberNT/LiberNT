
include_directories(BEFORE ${REACTOS_SOURCE_DIR}/sdk/include/wine)
add_definitions(-D__WINESRC__)

add_executable(start start.c start.rc)
set_module_type(start win32cui UNICODE)
target_link_libraries(start wine)
add_importlibs(start shell32 user32 msvcrt kernel32 ntdll)
add_cd_file(TARGET start DESTINATION reactos/system32 FOR all)
