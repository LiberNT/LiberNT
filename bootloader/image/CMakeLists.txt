##
## PROJECT:     LiberNT Boot Loader
## LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
## PURPOSE:     EFI system partition, BCD store and GPT disk image
## COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
##

set(BOOTLOADER_TIMEOUT 3 CACHE STRING "Seconds the boot menu waits before starting the default entry")
option(BOOTLOADER_DISPLAY_MENU "Show the boot menu even with a single entry" ON)
option(BOOTLOADER_KERNEL_DEBUG "Start the kernel with the serial debugger and verbose loading" OFF)
set(BOOTLOADER_LOAD_OPTIONS "KDWATCHDOG=0" CACHE STRING "Extra kernel load options of the default entry")
set(BOOTLOADER_KERNEL "ntkrnlmp.exe" CACHE STRING "Kernel image of the default entry")
if(ARCH STREQUAL "i386")
    set(BOOTLOADER_HAL "halmacpi.dll" CACHE STRING "HAL image of the default entry")
else()
    set(BOOTLOADER_HAL "hal.dll" CACHE STRING "HAL image of the default entry")
endif()
if(ARCH STREQUAL "ppc")
    set(BOOTLOADER_OS_LOADER "winload.exe")
else()
    set(BOOTLOADER_OS_LOADER "winload.efi")
endif()

set(BOOTLOADER_ESP_SECTORS 131072)
set(BOOTLOADER_ESP_START 2048)
math(EXPR BOOTLOADER_SYSTEM_START "${BOOTLOADER_ESP_START} + ${BOOTLOADER_ESP_SECTORS}")

set(BOOTLOADER_ID_NAMESPACE 354fa6e5-5e45-49af-91bf-1fd2754e470a)
set(BCD_OS_ENTRY "{${BOOTLOADER_ID_NAMESPACE}}")
set(BCD_DEFAULT_ENTRY "${BCD_OS_ENTRY}")
set(BCD_DISPLAY_ORDER "\"${BCD_OS_ENTRY}\"")
set(BCD_HTTP_ENTRY "")
string(UUID BOOTLOADER_DISK_ID NAMESPACE ${BOOTLOADER_ID_NAMESPACE} NAME "LiberNT disk" TYPE SHA1)
string(UUID BOOTLOADER_ESP_ID NAMESPACE ${BOOTLOADER_ID_NAMESPACE} NAME "LiberNT EFI system partition" TYPE SHA1)
string(UUID BOOTLOADER_SYSTEM_ID NAMESPACE ${BOOTLOADER_ID_NAMESPACE} NAME "LiberNT system partition" TYPE SHA1)

function(bootloader_guid_bytes _out _guid)
    string(REPLACE "-" "" _hex "${_guid}")
    string(TOLOWER "${_hex}" _hex)
    foreach(_index 3 2 1 0 5 4 7 6 8 9 10 11 12 13 14 15)
        math(EXPR _offset "${_index} * 2")
        string(SUBSTRING "${_hex}" ${_offset} 2 _byte)
        list(APPEND _bytes ${_byte})
    endforeach()
    list(JOIN _bytes "," _joined)
    set(${_out} "${_joined}" PARENT_SCOPE)
endfunction()

function(bootloader_partition_device _out _partition_guid _disk_guid)
    bootloader_guid_bytes(_partition ${_partition_guid})
    bootloader_guid_bytes(_disk ${_disk_guid})
    set(_zero "00,00,00,00,00,00,00,00")
    set(${_out}
        "${_zero},${_zero},06,00,00,00,00,00,00,00,48,00,00,00,00,00,00,00,${_partition},${_zero},${_disk},${_zero},${_zero}"
        PARENT_SCOPE)
endfunction()

function(bootloader_integer _out _value)
    math(EXPR _low "${_value} & 255")
    math(EXPR _high "(${_value} >> 8) & 255")
    math(EXPR _low "${_low}" OUTPUT_FORMAT HEXADECIMAL)
    math(EXPR _high "${_high}" OUTPUT_FORMAT HEXADECIMAL)
    string(REPLACE "0x" "" _low "${_low}")
    string(REPLACE "0x" "" _high "${_high}")
    set(${_out} "${_low},${_high},00,00,00,00,00,00" PARENT_SCOPE)
endfunction()

function(bootloader_bcd_store _directory)
    configure_file(bcd.inf.in ${_directory}/bcd.inf @ONLY)

    add_custom_command(
        OUTPUT ${_directory}/bcd_utf16.inf
        COMMAND native-utf16le ${_directory}/bcd.inf ${_directory}/bcd_utf16.inf
        DEPENDS native-utf16le ${_directory}/bcd.inf)

    add_custom_command(
        OUTPUT ${_directory}/BCD
        COMMAND native-mkhive -h:BCD -u -d:${_directory} ${_directory}/bcd_utf16.inf
        DEPENDS native-mkhive ${_directory}/bcd_utf16.inf)
endfunction()

bootloader_partition_device(BCD_BOOT_DEVICE ${BOOTLOADER_ESP_ID} ${BOOTLOADER_DISK_ID})
bootloader_partition_device(BCD_OS_DEVICE ${BOOTLOADER_SYSTEM_ID} ${BOOTLOADER_DISK_ID})
set(BCD_LOADER_DEVICE "${BCD_OS_DEVICE}")
set(BCD_LOADER_PATH "\\Windows\\system32\\${BOOTLOADER_OS_LOADER}")
set(BCD_KERNEL "${BOOTLOADER_KERNEL}")
set(BCD_HAL "${BOOTLOADER_HAL}")
set(BCD_LOAD_OPTIONS "${BOOTLOADER_LOAD_OPTIONS}")
set(BCD_WINPE_ELEMENT "")
bootloader_integer(BCD_TIMEOUT ${BOOTLOADER_TIMEOUT})

if(BOOTLOADER_DISPLAY_MENU)
    set(BCD_DISPLAY_MENU 01)
else()
    set(BCD_DISPLAY_MENU 00)
endif()

if(BOOTLOADER_KERNEL_DEBUG)
    set(BCD_DEBUG 01)
    set(BCD_SOS 01)
else()
    set(BCD_DEBUG 00)
    set(BCD_SOS 00)
endif()

if(ARCH STREQUAL "amd64")
    set(_efi_name bootx64.efi)
elseif(ARCH STREQUAL "i386")
    set(_efi_name bootia32.efi)
elseif(ARCH STREQUAL "arm64")
    set(_efi_name bootaa64.efi)
else()
    set(_efi_name boot${ARCH}.efi)
endif()

bootloader_bcd_store(${CMAKE_CURRENT_BINARY_DIR})

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/esp.fat
    COMMAND native-fatten ${CMAKE_CURRENT_BINARY_DIR}/esp.fat
        -format ${BOOTLOADER_ESP_SECTORS} fat32 LIBERNT
        -mkdir EFI
        -mkdir EFI/BOOT
        -mkdir EFI/LiberNT
        -mkdir EFI/LiberNT/Boot
        -add $<TARGET_FILE:bootmgfw> EFI/BOOT/${_efi_name}
        -add $<TARGET_FILE:bootmgfw> EFI/LiberNT/Boot/bootmgfw.efi
        -add ${CMAKE_CURRENT_BINARY_DIR}/BCD EFI/LiberNT/Boot/BCD
    DEPENDS native-fatten bootmgfw ${CMAKE_CURRENT_BINARY_DIR}/BCD
    VERBATIM)

add_custom_target(bootloaderesp DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/esp.fat)

if(NOT TARGET winload)
    return()
endif()

add_custom_target(bootloaderimg
    COMMAND native-mkdiskimg -gpt -diskid ${BOOTLOADER_DISK_ID}
        -o ${CMAKE_CURRENT_BINARY_DIR}/LiberNT.img
        -partition ${CMAKE_CURRENT_BINARY_DIR}/esp.fat
        -start ${BOOTLOADER_ESP_START}
        -guid c12a7328-f81f-11d2-ba4b-00a0c93ec93b
        -id ${BOOTLOADER_ESP_ID}
        -name "EFI system partition"
        -partition ${REACTOS_BINARY_DIR}/boot/partition.ntfs
        -start ${BOOTLOADER_SYSTEM_START}
        -guid ebd0a0a2-b9e5-4433-87c0-68b6b72699c7
        -id ${BOOTLOADER_SYSTEM_ID}
        -name LiberNT
    DEPENDS native-mkdiskimg ${CMAKE_CURRENT_BINARY_DIR}/esp.fat
    VERBATIM)

set(BOOTLOADER_MEDIA_RAMDISK_EXTRA "64M" CACHE STRING "Writable space added to the RAM disk of the live medium")
set(BOOTLOADER_MEDIA_SECTORS 8192)

set(_zero "00,00,00,00,00,00,00,00")
set(_boot_device "05,00,00,00,00,00,00,00,48,00,00,00,00,00,00,00,${_zero},${_zero},${_zero},${_zero},${_zero},${_zero},${_zero}")
set(_ramdisk_header "${_zero},${_zero},00,00,00,00,01,00,00,00")
set(_ramdisk_body "00,00,00,00,03,00,00,00,00,00,00,00,${_zero},${_zero},01,00,00,00")
set(BCD_BOOT_DEVICE "${_zero},${_zero},${_boot_device}")
set(BCD_LOADER_DEVICE "${BCD_BOOT_DEVICE}")
set(BCD_LOADER_PATH "\\EFI\\LiberNT\\Boot\\winload.efi")
set(BCD_LOAD_OPTIONS "RDRAMSIZE=${BOOTLOADER_MEDIA_RAMDISK_EXTRA} ${BOOTLOADER_LOAD_OPTIONS}")
set(BCD_WINPE_ELEMENT
    "BCD,\"BCD00000000\\Objects\\${BCD_OS_ENTRY}\\Elements\\26000022\",\"Element\",0x1,01")

function(bootloader_media _target _directory _image)
    bootloader_bcd_store(${_directory})

    add_custom_target(${_target}
        COMMAND native-fatten ${_directory}/efisys.bin
            -format ${BOOTLOADER_MEDIA_SECTORS} fat EFIBOOT
            -mkdir EFI
            -mkdir EFI/BOOT
            -mkdir EFI/LiberNT
            -mkdir EFI/LiberNT/Boot
            -add $<TARGET_FILE:bootmgfw> EFI/BOOT/${_efi_name}
            -add $<TARGET_FILE:winload> EFI/LiberNT/Boot/winload.efi
            -add ${_directory}/BCD EFI/LiberNT/Boot/BCD
        COMMAND ${CMAKE_COMMAND} -DMODE=list
            -DBASE=${REACTOS_BINARY_DIR}/boot/livecd.$<CONFIG>.lst
            -DOPTIONAL=${REACTOS_BINARY_DIR}/boot/livecd.optional.$<CONFIG>.lst
            -DOUTPUT=${_directory}/files.lst
            -DSYSTEM_DIR=Windows
            -P ${REACTOS_SOURCE_DIR}/sdk/cmake/optional_files.cmake
        COMMAND ${CMAKE_COMMAND}
            -DLIST=${_directory}/files.lst
            -DEFISYS=${_directory}/efisys.bin
            ${ARGN}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/medialist.cmake
        COMMAND native-mkisofs -quiet -o ${_image}
            -iso-level 4 -relaxed-filenames
            -publisher "LiberNT Project" -preparer "LiberNT Project" -volid LiberNT -volset LiberNT
            -eltorito-platform efi -eltorito-boot efi/efisys.bin -no-emul-boot
            -hide boot.catalog
            -duplicates-once -no-cache-inodes -graft-points
            -path-list ${_directory}/files.lst
        DEPENDS native-fatten native-mkisofs native-wimage bootmgfw winload ${_directory}/BCD
        VERBATIM)

    if(TARGET livecd_hives)
        add_dependencies(${_target} livecd_hives)
    endif()
endfunction()

function(bootloader_dword _out _value)
    set(_bytes "")
    foreach(_shift 0 8 16 24)
        math(EXPR _byte "((${_value} >> ${_shift}) & 255) + 256" OUTPUT_FORMAT HEXADECIMAL)
        string(SUBSTRING "${_byte}" 3 2 _byte)
        list(APPEND _bytes ${_byte})
    endforeach()
    list(JOIN _bytes "," _joined)
    set(${_out} "${_joined}" PARENT_SCOPE)
endfunction()

function(bootloader_ramdisk_device _out _path)
    string(LENGTH "${_path}" _chars)
    string(HEX "${_path}" _hex)
    set(_text "")
    math(EXPR _index 0)
    while(_index LESS _chars)
        math(EXPR _offset "${_index} * 2")
        string(SUBSTRING "${_hex}" ${_offset} 2 _byte)
        string(APPEND _text "${_byte},00,")
        math(EXPR _index "${_index} + 1")
    endwhile()
    math(EXPR _text_size "(${_chars} + 1) * 2")
    bootloader_dword(_device_size "0x7c + ${_text_size}")
    bootloader_dword(_file_size "0x54 + ${_text_size}")
    set(${_out}
        "${_ramdisk_header},${_device_size},${_ramdisk_body},${_file_size},05,00,00,00,${_boot_device},${_text}00,00"
        PARENT_SCOPE)
endfunction()

bootloader_ramdisk_device(BCD_OS_DEVICE "")
bootloader_media(bootloadercd ${CMAKE_CURRENT_BINARY_DIR}/media ${CMAKE_CURRENT_BINARY_DIR}/LiberNT.iso)

bootloader_ramdisk_device(BCD_OS_DEVICE "\\boot.wim")
bootloader_media(bootloaderwimcd ${CMAKE_CURRENT_BINARY_DIR}/mediawim ${CMAKE_CURRENT_BINARY_DIR}/LiberNT-wim.iso
    -DWIM=${CMAKE_CURRENT_BINARY_DIR}/mediawim/boot.wim
    -DSTAGE=${CMAKE_CURRENT_BINARY_DIR}/mediawim/stage
    -DWIMAGE=$<TARGET_FILE:native-wimage>)

add_custom_target(bootloadermediabcd DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/media/BCD)
add_custom_target(bootloadermediawimbcd DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/mediawim/BCD)

get_property(_standard_signature GLOBAL PROPERTY LIBERNT_STANDARD_SIGNATURE)
get_property(_standard_start GLOBAL PROPERTY LIBERNT_STANDARD_SYSTEM_START)
get_property(_standard_options GLOBAL PROPERTY LIBERNT_STANDARD_OPTIONS)
get_property(_standard_gpt_disk GLOBAL PROPERTY LIBERNT_STANDARD_GPT_DISK)
get_property(_standard_gpt_system GLOBAL PROPERTY LIBERNT_STANDARD_GPT_SYSTEM)

if(_standard_signature)
    if(_standard_gpt_disk)
        bootloader_partition_device(BCD_OS_DEVICE ${_standard_gpt_system} ${_standard_gpt_disk})
    else()
        math(EXPR _offset_low "(${_standard_start} * 512) & 0xFFFFFFFF")
        math(EXPR _offset_high "(${_standard_start} * 512) >> 32")
        bootloader_dword(_offset_low ${_offset_low})
        bootloader_dword(_offset_high ${_offset_high})
        bootloader_dword(_signature "0x${_standard_signature}")
        set(BCD_OS_DEVICE
            "${_zero},${_zero},06,00,00,00,00,00,00,00,48,00,00,00,00,00,00,00,${_offset_low},${_offset_high},${_zero},00,00,00,00,01,00,00,00,${_signature},${_zero},${_zero},${_zero},00,00,00,00")
    endif()

    string(REGEX REPLACE "(^| )/" "\\1" _standard_options "${_standard_options}")
    set(BCD_LOADER_DEVICE "${BCD_OS_DEVICE}")
    set(BCD_LOADER_PATH "\\Windows\\system32\\${BOOTLOADER_OS_LOADER}")
    set(BCD_LOAD_OPTIONS "KDWATCHDOG=0 ${_standard_options}")
    string(STRIP "${BCD_LOAD_OPTIONS}" BCD_LOAD_OPTIONS)
    set(BCD_WINPE_ELEMENT "")
    set(BCD_SOS 00)
    set(BCD_DEBUG 00)

    if(BOOTLOADER_HTTP_BOOT_URL)
        string(UUID _http_entry NAMESPACE ${BOOTLOADER_ID_NAMESPACE} NAME "LiberNT HTTP boot" TYPE SHA1)
        set(_http_entry "{${_http_entry}}")
        bootloader_ramdisk_device(_http_device "")
        set(_http_options "RDRAMSIZE=${BOOTLOADER_MEDIA_RAMDISK_EXTRA} HTTPBOOTURL=${BOOTLOADER_HTTP_BOOT_URL}")
        if(BOOTLOADER_HTTP_BOOT_IP)
            string(APPEND _http_options " HTTPBOOTIP=${BOOTLOADER_HTTP_BOOT_IP}")
        endif()
        string(APPEND _http_options " ${BCD_LOAD_OPTIONS}")
        set(_object "BCD,\"BCD00000000\\Objects\\${_http_entry}")
        set(BCD_HTTP_ENTRY "
${_object}\\Description\",\"Type\",0x00010001,0x10200003
${_object}\\Elements\\11000001\",\"Element\",0x1,${BCD_BOOT_DEVICE}
${_object}\\Elements\\12000002\",\"Element\",,\"\\EFI\\LiberNT\\Boot\\${BOOTLOADER_OS_LOADER}\"
${_object}\\Elements\\12000004\",\"Element\",,\"LiberNT (HTTP boot)\"
${_object}\\Elements\\12000005\",\"Element\",,\"en-US\"
${_object}\\Elements\\14000006\",\"Element\",0x10000,\"{6efb52bf-1766-41db-a6b3-0ee5eff72bd7}\"
${_object}\\Elements\\16000009\",\"Element\",0x1,00
${_object}\\Elements\\16000060\",\"Element\",0x1,01
${_object}\\Elements\\21000001\",\"Element\",0x1,${_http_device}
${_object}\\Elements\\22000002\",\"Element\",,\"\\Windows\"
${_object}\\Elements\\22000011\",\"Element\",,\"${BCD_KERNEL}\"
${_object}\\Elements\\22000012\",\"Element\",,\"${BCD_HAL}\"
${_object}\\Elements\\25000020\",\"Element\",0x1,00,00,00,00,00,00,00,00
${_object}\\Elements\\250000c2\",\"Element\",0x1,00,00,00,00,00,00,00,00
${_object}\\Elements\\26000022\",\"Element\",0x1,01
${_object}\\Elements\\26000091\",\"Element\",0x1,${BCD_SOS}
${_object}\\Elements\\260000a0\",\"Element\",0x1,${BCD_DEBUG}
${_object}\\Elements\\12000030\",\"Element\",,\"${_http_options}\"")
        set(BCD_DISPLAY_ORDER "\"${BCD_OS_ENTRY}\",\"${_http_entry}\"")
        if(BOOTLOADER_HTTP_BOOT_DEFAULT)
            set(BCD_DEFAULT_ENTRY "${_http_entry}")
        endif()
    endif()

    bootloader_bcd_store(${CMAKE_CURRENT_BINARY_DIR}/standard)
    add_custom_target(bootloaderstandardbcd DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/standard/BCD)
endif()

set(BCD_BOOT_DEVICE "${_zero},${_zero},${_boot_device}")
set(BCD_OS_DEVICE "${BCD_BOOT_DEVICE}")
set(BCD_LOADER_DEVICE "${BCD_BOOT_DEVICE}")
set(BCD_LOADER_PATH "\\Windows\\system32\\${BOOTLOADER_OS_LOADER}")
set(BCD_LOAD_OPTIONS "${BOOTLOADER_LOAD_OPTIONS}")
set(BCD_WINPE_ELEMENT "")
set(BCD_SOS 00)
set(BCD_DEBUG 00)
set(BCD_DEFAULT_ENTRY "${BCD_OS_ENTRY}")
set(BCD_DISPLAY_ORDER "\"${BCD_OS_ENTRY}\"")
set(BCD_HTTP_ENTRY "")
bootloader_bcd_store(${CMAKE_CURRENT_BINARY_DIR}/template)
add_custom_target(bootloadertemplatebcd DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/template/BCD)
add_cd_file(TARGET bootloadertemplatebcd FILE ${CMAKE_CURRENT_BINARY_DIR}/template/BCD
            DESTINATION reactos/system32/config NO_CAB NAME_ON_CD BCD-Template FOR all)
