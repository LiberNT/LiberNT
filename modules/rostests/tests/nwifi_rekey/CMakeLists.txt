add_executable(nwifi_rekey nwifi_rekey.c)
target_include_directories(nwifi_rekey PRIVATE
    ${REACTOS_SOURCE_DIR}/drivers/network/nwifi
    ${REACTOS_SOURCE_DIR}/sdk/lib/drivers/rsna_supplicant)
target_compile_definitions(nwifi_rekey PRIVATE NDIS620_MINIPORT NDIS_MINIPORT_DRIVER NDIS_LEGACY_DRIVER=1)
if(MSVC)
    target_compile_options(nwifi_rekey PRIVATE /Gy /Gw)
else()
    target_compile_options(nwifi_rekey PRIVATE -ffunction-sections -fdata-sections)
endif()
set_module_type(nwifi_rekey win32cui)
target_link_libraries(nwifi_rekey rsna_supplicant)
add_importlibs(nwifi_rekey bcrypt ntdll msvcrt kernel32)
