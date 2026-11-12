#
# LiberNT Native WiFi (802.11) driver - nwifi.sys
# NDIS 6.20 media-converting filter: 802.3 above, Native 802.11 below.
#

add_definitions(
    -DNDIS620
    -DNTDDI_VERSION=0x06010000)

# WPA2-PSK (RSNA) supplicant static library (rsna_supplicant.h)
include_directories(${REACTOS_SOURCE_DIR}/sdk/lib/drivers/rsna_supplicant)

list(APPEND SOURCE
    nwifi.c
    filter.c
    datapath.c
    oid.c
    msm.c
    control.c
    supplicant.c
    nwifi.h)

add_library(nwifi MODULE
    ${SOURCE}
    nwifi.rc)

set_module_type(nwifi kernelmodedriver)
target_link_libraries(nwifi ${PSEH_LIB} rsna_supplicant)

add_importlibs(nwifi ndis ntoskrnl hal cng)
add_pch(nwifi nwifi.h "")
add_cd_file(TARGET nwifi DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(nwifi nwifi.inf)
