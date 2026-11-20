
include_directories(../include)

#
# KeWaitTerminate
#
list(APPEND KEWAITTERMINATE_DRV_SOURCE
    ../kmtest_drv/kmtest_standalone.c
    KeWaitTerminate_drv.c)

add_library(kewaitterminate_drv MODULE ${KEWAITTERMINATE_DRV_SOURCE})
set_module_type(kewaitterminate_drv kernelmodedriver)
target_link_libraries(kewaitterminate_drv kmtest_printf ${PSEH_LIB})
add_importlibs(kewaitterminate_drv ntoskrnl hal)
target_compile_definitions(kewaitterminate_drv PRIVATE KMT_STANDALONE_DRIVER)
add_rostests_file(TARGET kewaitterminate_drv)
