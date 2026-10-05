
add_library(dbgcap MODULE dbgcap.c dbgcap.rc)
target_link_libraries(dbgcap wdmsec)
set_module_type(dbgcap kernelmodedriver)
add_importlibs(dbgcap ntoskrnl)
add_cd_file(TARGET dbgcap DESTINATION reactos/system32/drivers FOR all)
add_registry_inf(dbgcap_reg.inf)
