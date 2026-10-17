##
## PROJECT:     LiberNT Boot Loader
## LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
## PURPOSE:     Build definitions
## COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
##

if(ARCH STREQUAL "i386" AND (SARCH STREQUAL "pc98" OR SARCH STREQUAL "xbox"))
    return()
endif()

include_directories(BEFORE include)
include_directories(
    ${REACTOS_SOURCE_DIR}/ntoskrnl/include
    ${REACTOS_SOURCE_DIR}/sdk/lib/cmlib
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/edk2)

add_definitions(-D_NTHAL_ -D_BLDR_ -D_NTSYSTEM_ -DNTDDI_VERSION=${REACTOS_TARGET_NTDDI})

function(bootloader_ofw_application _target _base)
    set_target_properties(${_target} PROPERTIES SUFFIX ".exe")
    set_image_base(${_target} ${_base})
    target_link_options(${_target} PRIVATE -Wl,--exclude-all-symbols,--file-alignment,0x200,--section-alignment,0x1000)
    set_subsystem(${_target} native)
    set_entrypoint(${_target} OfwEntry)
    target_link_libraries(${_target} bootlib blcmlib blrtl libcntpr setjmp)
    add_dependencies(${_target} xdk)
endfunction()

function(bootloader_efi_application _target _entry)
    set_target_properties(${_target} PROPERTIES SUFFIX ".efi")

    if(ARCH STREQUAL "arm64" OR ARCH STREQUAL "riscv64")
        target_link_options(${_target} PRIVATE -Wl,--exclude-all-symbols,--file-alignment,0x200,--section-alignment,0x1000)
    else()
        target_link_options(${_target} PRIVATE -Wl,--exclude-all-symbols,--file-alignment,0x200,--section-alignment,0x200)
    endif()

    if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
        target_link_options(${_target} PRIVATE -Wl,/driver)
        if(ARCH STREQUAL "arm64")
            target_link_options(${_target} PRIVATE
                -Wl,--subsystem,efi_application:6.00
                -Wl,--major-os-version,6
                -Wl,--minor-os-version,0
                -Wl,--major-subsystem-version,6
                -Wl,--minor-subsystem-version,0)
        else()
            set_subsystem(${_target} efi_application)
        endif()
    else()
        set_subsystem(${_target} 10)
    endif()

    set_entrypoint(${_target} ${_entry})
    target_link_libraries(${_target} bootlib blcmlib blrtl libcntpr)
    if(ARCH STREQUAL "i386")
        target_link_libraries(${_target} mini_hal)
    endif()
    if(ARCH STREQUAL "arm64")
        set_target_properties(${_target} PROPERTIES REACTOS_LSE_ATOMICS lse_atomics_boot)
    endif()
    add_dependencies(${_target} xdk)
endfunction()

list(APPEND BOOTLIB_SOURCE
    lib/bcd.c
    lib/disk.c
    lib/entries.c
    lib/exfat.c
    lib/fs.c
    lib/gpt.c
    lib/libsupp.c
    lib/ntfs.c)

if(ARCH STREQUAL "ppc")
    add_asm_files(bootlib_asm ofw/ofwgate.S)
    list(APPEND BOOTLIB_SOURCE
        ${bootlib_asm}
        ofw/ofw.c
        ofw/platform.c)
    list(APPEND BOOTGUI_FIRMWARE_SOURCE
        ofw/console.c)
else()
    list(APPEND BOOTLIB_SOURCE
        uefi/platform.c
        uefi/serial.c
        uefi/uefi.c)
    list(APPEND BOOTGUI_FIRMWARE_SOURCE
        uefi/console.c
        uefi/uefiinput.c
        uefi/uefireboot.c)
endif()

if(ARCH STREQUAL "riscv64")
    list(APPEND BOOTLIB_SOURCE arch/riscv64/runtime.c)
elseif(ARCH STREQUAL "arm64")
    list(APPEND BOOTLIB_SOURCE arch/arm64/stubs.c)
elseif(ARCH STREQUAL "ppc")
    list(APPEND BOOTLIB_SOURCE arch/ppc/stubs.c)
endif()

add_library(bootlib ${BOOTLIB_SOURCE})
target_link_libraries(bootlib fatfs)
add_dependencies(bootlib bugcodes asm xdk)

foreach(_asset font mono icons logo)
    if(_asset STREQUAL "font")
        set(_asset_source ${REACTOS_SOURCE_DIR}/media/fonts/selawk.ttf)
    elseif(_asset STREQUAL "mono")
        set(_asset_source ${REACTOS_SOURCE_DIR}/media/fonts/lucon.ttf)
    elseif(_asset STREQUAL "icons")
        set(_asset_source ${CMAKE_CURRENT_SOURCE_DIR}/gui/assets/icons.alpha)
    else()
        set(_asset_source ${CMAKE_CURRENT_SOURCE_DIR}/gui/assets/logo.rgba)
    endif()
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/bootui_${_asset}.c ${CMAKE_CURRENT_BINARY_DIR}/bootui_${_asset}.h
        COMMAND native-bin2c ${_asset_source}
                ${CMAKE_CURRENT_BINARY_DIR}/bootui_${_asset}.c
                ${CMAKE_CURRENT_BINARY_DIR}/bootui_${_asset}.h BIN bootui_${_asset}
        DEPENDS native-bin2c ${_asset_source})
    list(APPEND BOOTGUI_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/bootui_${_asset}.c)
endforeach()

if(ARCH STREQUAL "arm64")
    list(APPEND BOOTGUI_SOURCE arch/arm64/unwind.c)
endif()

add_library(bootgui
    ${BOOTGUI_SOURCE}
    ${BOOTGUI_FIRMWARE_SOURCE}
    gui/bmui.c
    gui/bootui.c
    gui/ftglue.c
    gui/guisupp.c)
target_include_directories(bootgui PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(bootgui setjmp freetype)
add_dependencies(bootgui bugcodes asm xdk)

add_library(bootwim
    ${REACTOS_SOURCE_DIR}/sdk/lib/libwim/lzx.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/libwim/sha1.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/libwim/wimcore.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/libwim/xpress_huff.c)
set_property(TARGET bootwim PROPERTY C_STANDARD 11)
target_include_directories(bootwim PRIVATE ${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/libwim)
target_compile_definitions(bootwim PRIVATE XPRESS_HUFF_DECOMPRESS_ONLY=1)
add_dependencies(bootwim xdk)

add_executable(bootmgfw
    bootmgr/app.c
    bootmgr/logo.c
    bootmgr/main.c
    bootmgr/startup.c)
if(ARCH STREQUAL "ppc")
    set_target_properties(bootmgfw PROPERTIES OUTPUT_NAME bootmgr)
    bootloader_ofw_application(bootmgfw 0x00100000)
else()
    bootloader_efi_application(bootmgfw EfiEntry)
endif()
target_link_libraries(bootmgfw bootgui bootwim)

add_custom_target(bootloader DEPENDS bootmgfw)

if(ARCH STREQUAL "ppc")
    add_custom_command(TARGET bootmgfw
        POST_BUILD
        COMMAND ${CMAKE_STRIP} --remove-section=.symz $<TARGET_FILE:bootmgfw>
        COMMAND ${CMAKE_STRIP} --strip-all $<TARGET_FILE:bootmgfw>)

    get_filename_component(_ppc_llvm_bin ${CMAKE_C_COMPILER} DIRECTORY)
    set(_stage0_dir ${CMAKE_CURRENT_SOURCE_DIR}/arch/ppc/stage0)
    set(_stage0_flags --target=powerpc-unknown-elf -mcpu=604 -O2 -ffreestanding -fno-builtin -fno-pic -msoft-float -fno-stack-protector)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/bootmgr.bin
        COMMAND ${CMAKE_C_COMPILER} ${_stage0_flags} -c ${_stage0_dir}/stage0.c -o ${CMAKE_CURRENT_BINARY_DIR}/stage0_c.o
        COMMAND ${CMAKE_C_COMPILER} ${_stage0_flags} -DSTAGE0_PAYLOAD=\"$<TARGET_FILE:bootmgfw>\" -c ${_stage0_dir}/stage0.S -o ${CMAKE_CURRENT_BINARY_DIR}/stage0_s.o
        COMMAND ${_ppc_llvm_bin}/ld.lld -m elf32ppc -N -T ${_stage0_dir}/stage0.ld ${CMAKE_CURRENT_BINARY_DIR}/stage0_s.o ${CMAKE_CURRENT_BINARY_DIR}/stage0_c.o -o ${CMAKE_CURRENT_BINARY_DIR}/bootmgr.elf
        COMMAND ${_ppc_llvm_bin}/llvm-objcopy -O binary ${CMAKE_CURRENT_BINARY_DIR}/bootmgr.elf ${CMAKE_CURRENT_BINARY_DIR}/bootmgr.bin
        DEPENDS bootmgfw ${_stage0_dir}/stage0.c ${_stage0_dir}/stage0.S ${_stage0_dir}/stage0.ld
        VERBATIM)
    add_custom_target(bootmgrppc DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/bootmgr.bin)
    add_dependencies(bootloader bootmgrppc)
endif()

if(ARCH STREQUAL "arm64")
    list(APPEND WINLOAD_ASM_SOURCE
        arch/arm64/cache_v2.S
        arch/arm64/entry.S
        arch/arm64/except.S
        arch/arm64/kernel_jump.S
        arch/arm64/misc.S
        arch/arm64/uefiasm.S
        arch/arm64/uefitrap.S)

    list(APPEND WINLOAD_SOURCE
        arch/arm64/archmm.c
        arch/arm64/early_uart.c
        arch/arm64/gic.c
        arch/arm64/macharm64.c
        arch/arm64/mmu_v2.c
        arch/arm64/timer.c
        arch/arm64/trap.c
        arch/arm64/uefitrap.c
        arch/arm64/winldr.c)
elseif(ARCH STREQUAL "riscv64")
    list(APPEND WINLOAD_ASM_SOURCE
        arch/riscv64/uefiasm.S)

    list(APPEND WINLOAD_SOURCE
        arch/riscv64/archosl.c
        arch/riscv64/jh7110.c
        arch/riscv64/watchdog.c
        arch/riscv64/winldr.c)
elseif(ARCH STREQUAL "amd64")
    list(APPEND WINLOAD_ASM_SOURCE
        arch/amd64/uefiasm.S)

    list(APPEND WINLOAD_SOURCE
        arch/amd64/archosl.c)
elseif(ARCH STREQUAL "i386")
    list(APPEND WINLOAD_ASM_SOURCE
        arch/i386/uefiasm.S)

    list(APPEND WINLOAD_SOURCE
        arch/i386/archosl.c)
elseif(ARCH STREQUAL "ppc")
    list(APPEND WINLOAD_ASM_SOURCE
        arch/ppc/ppcjump.S)

    list(APPEND WINLOAD_SOURCE
        arch/ppc/archosl.c)
endif()

if(NOT ARCH STREQUAL "ppc")
    list(APPEND WINLOAD_SOURCE
        net/uefidhcp.c
        net/uefihttp.c
        net/uefinet.c
        net/uefinet_hw.c
        net/snpnet.c
        uefi/memory.c)
endif()

if(WINLOAD_ASM_SOURCE)
    add_asm_files(winload_asm ${WINLOAD_ASM_SOURCE})

    add_executable(winload
        ${winload_asm}
        ${WINLOAD_SOURCE}
        ${REACTOS_SOURCE_DIR}/ntoskrnl/config/cmboot.c
        lib/mm.c
        lib/pe.c
        winload/hw.c
        winload/lpb.c
        winload/main.c
        winload/memory.c
        ramdisk/fatformat.c
        ramdisk/fatwrite.c
        ramdisk/wim.c
        winload/options.c
        winload/ramdisk.c
        winload/registry.c
        winload/smbios.c
        winload/startup.c)
    if(ARCH STREQUAL "ppc")
        bootloader_ofw_application(winload 0x00400000)
    else()
        bootloader_efi_application(winload EfiEntry)
    endif()
    target_link_libraries(winload bootwim)
    add_dependencies(bootloader winload)
    if(ARCH STREQUAL "riscv64")
        target_include_directories(winload PRIVATE ${REACTOS_SOURCE_DIR}/sdk/lib/drivers/jh7110disp)
        target_link_libraries(winload jh7110disp)
    endif()
    add_cd_file(TARGET winload DESTINATION reactos/system32 FOR all)
endif()

add_subdirectory(image)
