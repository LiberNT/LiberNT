
# TODO: the processor driver has no architecture layer for RISC-V64
if(ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64" OR ARCH STREQUAL "arm64")
    add_subdirectory(processr)
endif()
add_subdirectory(acpipagr)
