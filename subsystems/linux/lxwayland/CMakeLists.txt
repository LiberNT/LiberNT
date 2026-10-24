
if(ARCH STREQUAL "arm64")
    set(_lxwayland_target aarch64-linux-gnu)
    set(_lxwayland_flags "")
elseif(ARCH STREQUAL "riscv64")
    set(_lxwayland_target riscv64-linux-gnu)
    set(_lxwayland_flags -mcmodel=medany)
endif()

set(_lxwayland_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/lxwayland.c
    ${CMAKE_CURRENT_SOURCE_DIR}/wlproto.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lxrt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/${ARCH}/lxrtarch.c)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/lxwayland
    COMMAND ${CMAKE_C_COMPILER} --target=${_lxwayland_target} -std=c11 -O2 -Wall -ffreestanding -fno-stack-protector
            -fno-pic -fno-asynchronous-unwind-tables -fno-unwind-tables ${_lxwayland_flags} -nostdlib -static
            -fuse-ld=lld -Wl,--gc-sections -Wl,-e,_start -Wl,--build-id=none -o ${CMAKE_CURRENT_BINARY_DIR}/lxwayland
            ${_lxwayland_sources}
    DEPENDS ${_lxwayland_sources} ${CMAKE_CURRENT_SOURCE_DIR}/lxrt.h ${CMAKE_CURRENT_SOURCE_DIR}/wlproto.h
            ${CMAKE_CURRENT_SOURCE_DIR}/../include/lxwin.h
    VERBATIM)
add_custom_target(lxwayland_agent ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/lxwayland)
add_cd_file(TARGET lxwayland_agent FILE ${CMAKE_CURRENT_BINARY_DIR}/lxwayland DESTINATION reactos/system32/lxwayland NO_CAB FOR all)

foreach(_file lxapp gui-setup user-setup lxwayland.service lx-gui-setup.service)
    add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/${_file} DESTINATION reactos/system32/lxwayland NO_CAB FOR all)
endforeach()
