
include_directories(../include)

list(APPEND NTFSTXFRECOVERY_DRV_SOURCE
    ../kmtest_drv/kmtest_standalone.c
    NtfsTxfRecovery_drv.c)

add_library(ntfstxfrecovery_drv MODULE ${NTFSTXFRECOVERY_DRV_SOURCE})
set_module_type(ntfstxfrecovery_drv kernelmodedriver)
target_link_libraries(ntfstxfrecovery_drv kmtest_printf ${PSEH_LIB})
add_importlibs(ntfstxfrecovery_drv fltmgr ntoskrnl hal)
target_compile_definitions(ntfstxfrecovery_drv PRIVATE KMT_STANDALONE_DRIVER)
add_rostests_file(TARGET ntfstxfrecovery_drv)
