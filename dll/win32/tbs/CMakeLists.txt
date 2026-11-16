
spec2def(tbs.dll tbs.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    tbs.c
    ${CMAKE_CURRENT_BINARY_DIR}/tbs_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/tbs.def)

add_library(tbs MODULE ${SOURCE})
set_module_type(tbs win32dll ENTRYPOINT 0 UNICODE)
add_importlibs(tbs cfgmgr32 kernel32 ntdll)
add_cd_file(TARGET tbs DESTINATION reactos/system32 FOR all)
