
spec2def(vmbus.sys vmbus.spec ADD_IMPORTLIB)
add_asm_files(vmbus_asm hvcall.S)

list(APPEND SOURCE
    channel.c
    hv.c
    vmbus.c
    vmbus.h)

add_library(vmbus MODULE
    ${SOURCE}
    ${vmbus_asm}
    vmbus.rc
    ${CMAKE_CURRENT_BINARY_DIR}/vmbus.def)

set_module_type(vmbus kernelmodedriver)
target_link_libraries(vmbus wdmguid ntoskrnl_vista)
add_importlibs(vmbus ntoskrnl hal)
add_cd_file(TARGET vmbus DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(vmbus vmbus.inf)
