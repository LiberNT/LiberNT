
add_library(wificx MODULE wificx.cpp wificx.rc)
set_module_type(wificx kmdfdriver KMDF_VERSION 1.33)
target_include_directories(wificx PRIVATE
    ${REACTOS_SOURCE_DIR}/sdk/include/ddk/netcx/kmdf/adapter/2.6
    ${REACTOS_SOURCE_DIR}/sdk/include/psdk/netcx/shared/1.0
    ${REACTOS_SOURCE_DIR}/sdk/include/ddk/wificx/1.2
    ${REACTOS_SOURCE_DIR}/drivers/network/netadaptercx/inc
    ${REACTOS_SOURCE_DIR}/sdk/include/libernt/drivers/ndis
    ${REACTOS_SOURCE_DIR}/sdk/lib/drivers/wdf
    ${REACTOS_SOURCE_DIR}/sdk/lib/drivers/wdf/kmdf/inc/private)
target_compile_definitions(wificx PRIVATE
    NET_VERSION_MAJOR=2
    NET_VERSION_MINOR=6
    WIFI_VERSION_MAJOR=1
    WIFI_VERSION_MINOR=2)
target_link_libraries(wificx netadaptercxstub_2_6 libcntpr ${PSEH_LIB})
add_importlibs(wificx ndis ntoskrnl hal)
add_cd_file(TARGET wificx DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_registry_inf(wificx_reg.inf)
