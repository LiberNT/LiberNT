include_directories(../../include)

list(APPEND WFPLISTEN_DRV_SOURCE
    ../../kmtest_drv/kmtest_standalone.c
    wfp_listen.c)

add_library(wfplisten_drv MODULE ${WFPLISTEN_DRV_SOURCE})
set_module_type(wfplisten_drv kernelmodedriver)
target_link_libraries(wfplisten_drv kmtest_printf uuid ${PSEH_LIB})
add_importlibs(wfplisten_drv fwpkclnt netio ndis ntoskrnl hal)
target_compile_definitions(wfplisten_drv PRIVATE KMT_STANDALONE_DRIVER NDIS630)
add_rostests_file(TARGET wfplisten_drv)
