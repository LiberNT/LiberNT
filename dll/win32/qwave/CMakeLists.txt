
spec2def(qwave.dll qwave.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    qwave.c
    ${CMAKE_CURRENT_BINARY_DIR}/qwave_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/qwave.def)

add_library(qwave MODULE ${SOURCE} qwave.rc)
set_module_type(qwave win32dll ENTRYPOINT 0 UNICODE)
add_importlibs(qwave advapi32 ws2_32 kernel32 ntdll)
add_cd_file(TARGET qwave DESTINATION reactos/system32 FOR all)
