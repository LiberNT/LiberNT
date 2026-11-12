include_directories(../../include)

list(APPEND WSKNAME_DRV_SOURCE
    ../../kmtest_drv/kmtest_standalone.c
    wskname.c)

add_library(wskname_drv MODULE ${WSKNAME_DRV_SOURCE})
set_module_type(wskname_drv kernelmodedriver)
target_link_libraries(wskname_drv kmtest_printf uuid ${PSEH_LIB})
add_importlibs(wskname_drv netio ndis ntoskrnl hal)
target_compile_definitions(wskname_drv PRIVATE KMT_STANDALONE_DRIVER NDIS630)
add_rostests_file(TARGET wskname_drv)
