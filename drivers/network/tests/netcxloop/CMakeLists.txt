
function(add_netcxloop _target _minor)
    add_library(${_target} MODULE netcxloop.cpp netcxloop.rc)
    set_module_type(${_target} kmdfdriver KMDF_VERSION 1.33)
    target_include_directories(${_target} PRIVATE
        ${REACTOS_SOURCE_DIR}/sdk/include/ddk/netcx/kmdf/adapter/2.${_minor}
        ${REACTOS_SOURCE_DIR}/sdk/include/psdk/netcx/shared/1.0)
    target_compile_definitions(${_target} PRIVATE NET_VERSION_MAJOR=2 NET_VERSION_MINOR=${_minor})
    target_link_libraries(${_target} netadaptercxstub_2_${_minor} libcntpr ${PSEH_LIB})
    add_importlibs(${_target} ntoskrnl hal)
endfunction()

add_netcxloop(netcxloop 5)
add_netcxloop(netcxloop23 3)

add_netcxloop(wifiloop 5)
target_include_directories(wifiloop PRIVATE ${REACTOS_SOURCE_DIR}/sdk/include/ddk/wificx/1.2)
target_compile_definitions(wifiloop PRIVATE NETCXLOOP_WIFI WIFI_VERSION_MAJOR=1 WIFI_VERSION_MINOR=2)
target_link_libraries(wifiloop wificxstub)

add_subdirectory(test)
