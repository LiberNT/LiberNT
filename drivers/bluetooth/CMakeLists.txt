
set(REALTEK_BT_FIRMWARE_DIR "" CACHE PATH "Directory holding Realtek Bluetooth rtl*_fw.bin and rtl*_config.bin files")

if(REALTEK_BT_FIRMWARE_DIR)
    file(GLOB _realtek_bt_firmware ${REALTEK_BT_FIRMWARE_DIR}/rtl*_fw.bin ${REALTEK_BT_FIRMWARE_DIR}/rtl*_config.bin)
    foreach(_realtek_bt_file ${_realtek_bt_firmware})
        add_cd_file(FILE ${_realtek_bt_file} DESTINATION reactos/system32/drivers/realtek NO_CAB FOR all)
    endforeach()
endif()

add_subdirectory(bthmini)
add_subdirectory(bthusb)
add_subdirectory(hidavrcp)
add_subdirectory(hidbth)
add_subdirectory(hidbthle)
add_subdirectory(rtkh5bt)
