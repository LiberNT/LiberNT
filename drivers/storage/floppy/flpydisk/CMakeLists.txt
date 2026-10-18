
add_library(flpydisk MODULE flpydisk.c flpydisk.rc)
set_module_type(flpydisk kernelmodedriver)
add_importlibs(flpydisk ntoskrnl hal)
add_cd_file(TARGET flpydisk DESTINATION reactos/system32/drivers NO_CAB FOR all)
