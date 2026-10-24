
project(HAL)

if(MSVC AND ARCH STREQUAL "amd64")
    add_definitions(
        -DKeGetCurrentIrql=HalpInlineGetCurrentIrql
        -DKeRaiseIrqlToSynchLevel=HalpInlineRaiseIrqlToSynchLevel
        -DREAD_PORT_BUFFER_UCHAR=HalpInlineReadPortBufferUchar
        -DREAD_PORT_BUFFER_USHORT=HalpInlineReadPortBufferUshort
        -DREAD_PORT_BUFFER_ULONG=HalpInlineReadPortBufferUlong
        -DREAD_PORT_UCHAR=HalpInlineReadPortUchar
        -DREAD_PORT_USHORT=HalpInlineReadPortUshort
        -DREAD_PORT_ULONG=HalpInlineReadPortUlong
        -DWRITE_PORT_BUFFER_UCHAR=HalpInlineWritePortBufferUchar
        -DWRITE_PORT_BUFFER_USHORT=HalpInlineWritePortBufferUshort
        -DWRITE_PORT_BUFFER_ULONG=HalpInlineWritePortBufferUlong
        -DWRITE_PORT_UCHAR=HalpInlineWritePortUchar
        -DWRITE_PORT_USHORT=HalpInlineWritePortUshort
        -DWRITE_PORT_ULONG=HalpInlineWritePortUlong)
endif()

add_subdirectory(arch/common)

if((ARCH STREQUAL "i386") OR (ARCH STREQUAL "amd64"))
    add_subdirectory(halx86)
elseif(ARCH STREQUAL "arm")
    add_subdirectory(halarm)
elseif(ARCH STREQUAL "arm64")
    add_subdirectory(halarm64)
elseif(ARCH STREQUAL "riscv64")
    add_subdirectory(halriscv64)
elseif(ARCH STREQUAL "ppc")
    add_subdirectory(halppc)
endif()
