
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/subsys)
add_executable(session_apitest StartSession.c testlist.c)
target_link_libraries(session_apitest smlib)
set_module_type(session_apitest win32cui)
add_importlibs(session_apitest advapi32 cfgmgr32 user32 kernel32 ntdll msvcrt)
add_rostests_file(TARGET session_apitest)

add_executable(session_child child.c)
set_module_type(session_child win32gui UNICODE)
add_importlibs(session_child user32 kernel32 msvcrt)
add_rostests_file(TARGET session_child)
