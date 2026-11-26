# Software-only virtual Native 802.11 (dot11) NDIS 6.20 miniport.

add_definitions(
    -DNDIS620_MINIPORT
    -DNDIS_MINIPORT_DRIVER
    -DNDIS_LEGACY_DRIVER=1)

include_directories(${REACTOS_SOURCE_DIR}/sdk/lib/drivers/rsna_supplicant)

list(APPEND SOURCE
    vwifi.c
    engine.c
    auth.c
    rxtx.c
    oid.c)

add_library(vwifi MODULE
    ${SOURCE}
    vwifi.rc)

set_module_type(vwifi kernelmodedriver)
target_link_libraries(vwifi ${PSEH_LIB} rsna_supplicant)

add_importlibs(vwifi ndis ntoskrnl hal)
add_pch(vwifi vwifi.h "")
add_cd_file(TARGET vwifi DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(vwifi netvwifi.inf)
