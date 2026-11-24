
add_definitions(-DNDIS60)

add_library(rspndr MODULE rspndr.c rspndr.rc)
set_module_type(rspndr kernelmodedriver)
add_importlibs(rspndr ndis ntoskrnl hal)
add_cd_file(TARGET rspndr DESTINATION reactos/system32/drivers FOR all)
