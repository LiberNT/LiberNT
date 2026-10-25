set(VC80_ASSEMBLY ${WINARCH}_microsoft.vc80.crt_1fc8b3b9a1e18e3b_8.0.50727.9680_none_deadbeef)
set(MSVCP90_DIR ${REACTOS_SOURCE_DIR}/dll/win32/msvcp90)

include_directories(BEFORE ${REACTOS_SOURCE_DIR}/sdk/include/wine)

include_directories(${MSVCP90_DIR})
add_definitions(-D_MSVCR_VER=80 -D_MSVCP_VER=80 -D__WINESRC__)

spec2def(msvcp80.dll msvcp80.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    ${MSVCP90_DIR}/exception.c
    ${MSVCP90_DIR}/ios.c
    ${MSVCP90_DIR}/locale.c
    ${MSVCP90_DIR}/math.c
    ${MSVCP90_DIR}/memory.c
    ${MSVCP90_DIR}/misc.c
    ${MSVCP90_DIR}/msvcp_main.c
    ${MSVCP90_DIR}/string.c
    ${CMAKE_CURRENT_BINARY_DIR}/msvcp80_stubs.c)

if(MSVC)
    add_compile_options(/wd4146 /wd4267 /wd4305)
    if(ARCH STREQUAL "i386")
        add_definitions(-D__ASM_USE_THISCALL_WRAPPER -D__ASM_EXTERN_THISCALL_WRAPPER)
        list(APPEND ASM_SOURCE
            ${MSVCP90_DIR}/i386/cpp_vtables.s
            ${MSVCP90_DIR}/i386/thiscall.s)
    elseif(ARCH STREQUAL "amd64")
        list(APPEND ASM_SOURCE
            ${MSVCP90_DIR}/amd64/cpp_vtables.s)
    elseif(ARCH STREQUAL "arm64")
        list(APPEND ASM_SOURCE
            ${MSVCP90_DIR}/arm64/cpp_vtables.s)
    endif()
    add_asm_files(msvcp80_asm ${ASM_SOURCE})
    list(APPEND SOURCE ${msvcp80_asm})
endif()

add_library(msvcp80 MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/msvcp80.def)

set_module_type(msvcp80 win32dll ENTRYPOINT DllMain 12)
target_link_libraries(msvcp80 wine)
add_importlibs(msvcp80 msvcr80 kernel32 ntdll)
add_dependencies(msvcp80 psdk)
add_cd_file(TARGET msvcp80 DESTINATION reactos/winsxs/${VC80_ASSEMBLY} FOR all)
