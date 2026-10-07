
add_subdirectory(kd_02_10ec)
add_subdirectory(kd_02_1af4)
add_subdirectory(kd_02_8086)
if((ARCH STREQUAL "riscv64") AND SPACEMIT_K1_SUPPORT)
    add_subdirectory(kd_fdt_spacemit_k1x-emac)
endif()

spec2def(kdnet.dll kdnet.spec)

include_directories(${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/symcrypt/lib)

list(APPEND SOURCE
    kdcrypto.c
    kdip.c
    kdnet.c
    kdnic.c
    kdplatform.c
    kdsession.c)

if(ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64")
    list(APPEND SOURCE x86/kdarch.c)
elseif(ARCH STREQUAL "arm64")
    list(APPEND SOURCE arm64/kdarch.c)
elseif(ARCH STREQUAL "riscv64")
    list(APPEND SOURCE riscv64/kdarch.c)
else()
    list(APPEND SOURCE generic/kdarch.c)
endif()

add_library(kdnet MODULE
    ${SOURCE}
    kdnet.rc
    ${CMAKE_CURRENT_BINARY_DIR}/kdnet.def)

target_compile_definitions(kdnet PRIVATE
    __WINE_PE_BUILD
    SYMCRYPT_PLATFORM_WINE=1)
set_module_type(kdnet kerneldll ENTRYPOINT 0)
set_subsystem(kdnet native)
target_link_libraries(kdnet symcrypt_km)
add_importlibs(kdnet ntoskrnl hal)
add_dependencies(kdnet psdk xdk bugcodes)
add_cd_file(TARGET kdnet DESTINATION reactos/system32 NO_CAB FOR all)
