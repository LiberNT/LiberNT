
list(APPEND SOURCE
    acpi.c
    guid.c
    pcc.c
    ppm.c
    processr.c
    tables.c)

if(ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64")
    list(APPEND SOURCE x86.c)
elseif(ARCH STREQUAL "arm64")
    list(APPEND SOURCE arm64.c)
endif()

add_library(processr MODULE ${SOURCE} processr.rc)
set_module_type(processr kernelmodedriver)
target_link_libraries(processr aux_klib)
add_importlibs(processr ntoskrnl hal)
add_cd_file(TARGET processr DESTINATION reactos/system32/drivers FOR all)
