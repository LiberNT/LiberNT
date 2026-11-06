
add_library(ndissg_drv MODULE NdisSgDma_drv.c)
set_module_type(ndissg_drv kernelmodedriver)
add_importlibs(ndissg_drv ndis ntoskrnl hal)
add_rostests_file(TARGET ndissg_drv)
configure_file(kmtest_ndissg.inf ${CMAKE_CURRENT_BINARY_DIR}/kmtest_ndissg.inf COPYONLY)
add_rostests_file(FILE "${CMAKE_CURRENT_BINARY_DIR}/kmtest_ndissg.inf")
