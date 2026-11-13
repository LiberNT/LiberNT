
include_directories(../include)

list(APPEND CMFLUSHREADERS_DRV_SOURCE
    ../kmtest_drv/kmtest_standalone.c
    CmFlushReaders_drv.c)

add_library(cmflushreaders_drv MODULE ${CMFLUSHREADERS_DRV_SOURCE})
set_module_type(cmflushreaders_drv kernelmodedriver)
target_link_libraries(cmflushreaders_drv kmtest_printf ${PSEH_LIB})
add_importlibs(cmflushreaders_drv fltmgr ntoskrnl hal)
target_compile_definitions(cmflushreaders_drv PRIVATE KMT_STANDALONE_DRIVER)
add_rostests_file(TARGET cmflushreaders_drv)
