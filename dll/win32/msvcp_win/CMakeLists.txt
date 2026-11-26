
spec2def(msvcp_win.dll msvcp_win.spec)

list(APPEND SOURCE
    ${CMAKE_CURRENT_BINARY_DIR}/msvcp_win_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/msvcp_win.def)

add_library(msvcp_win MODULE ${SOURCE})
set_module_type(msvcp_win win32dll)
target_link_libraries(msvcp_win wine)
add_importlibs(msvcp_win msvcrt kernel32 ntdll)
add_cd_file(TARGET msvcp_win DESTINATION reactos/system32 FOR all)
set_wine_module(msvcp_win)
