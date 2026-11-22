
spec2def(sas.dll sas.spec ADD_IMPORTLIB)

add_library(sas MODULE
    sas.c
    sas.rc
    ${CMAKE_CURRENT_BINARY_DIR}/sas.def)

set_module_type(sas win32dll ENTRYPOINT 0 UNICODE)
add_cd_file(TARGET sas DESTINATION reactos/system32 FOR all)
