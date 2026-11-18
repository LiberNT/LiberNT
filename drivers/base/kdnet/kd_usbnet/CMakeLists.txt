
list(APPEND SOURCE
    kdext.c
    proto.c
    tree.c
    usb.c
    usbnet.c
    xhci.c
    xhcidev.c)

add_library(kdusbnetcore ${SOURCE})
if(NOT MSVC)
    target_compile_options(kdusbnetcore PRIVATE -fno-builtin)
endif()
add_dependencies(kdusbnetcore psdk xdk bugcodes)

spec2def(kd_usbnet.dll kd_usbnet.spec)

add_library(kd_usbnet MODULE
    glue_pci.c
    kd_usbnet.rc
    ${CMAKE_CURRENT_BINARY_DIR}/kd_usbnet.def)

set_module_type(kd_usbnet kerneldll ENTRYPOINT 0)
set_subsystem(kd_usbnet native)
target_link_libraries(kd_usbnet kdusbnetcore libcntpr)
if(NOT MSVC)
    target_compile_options(kd_usbnet PRIVATE -fno-builtin)
endif()
add_dependencies(kd_usbnet psdk xdk)
add_cd_file(TARGET kd_usbnet DESTINATION reactos/system32 NO_CAB FOR all)
