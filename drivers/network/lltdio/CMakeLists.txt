
add_definitions(-DNDIS60)

add_library(lltdio MODULE lltdio.c lltdio.rc)
set_module_type(lltdio kernelmodedriver)
target_link_libraries(lltdio csq wdmsec)
add_importlibs(lltdio ndis ntoskrnl hal)
add_cd_file(TARGET lltdio DESTINATION reactos/system32/drivers FOR all)
