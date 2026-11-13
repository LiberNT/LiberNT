# Use the shared export catalog and the normal NT PE/COFF link path.
spec2def(hal.dll ../hal.spec ADD_IMPORTLIB)

add_library(hal MODULE
    cache.c
    clock.c
    dma.c
    halinit.c
    halpnpdd.c
    pci.c
    plic.c
    platform.c
    profile.c
    rtc.c
    sbi.c
    ../arch/common/generic/secondary.c
    ${REACTOS_SOURCE_DIR}/hal/arch/common/acpi/acpifault.c
    smp.c
    smp_trampoline.S
    stubs.c
    timer.c
    ${CMAKE_CURRENT_BINARY_DIR}/hal.def)
set_module_type(hal kerneldll ENTRYPOINT 0)
target_link_libraries(hal halcommon libcntpr)
add_importlibs(hal ntoskrnl)
target_compile_definitions(hal PRIVATE _NTHAL_ _NTHALDLL_)
target_include_directories(hal PRIVATE ${REACTOS_SOURCE_DIR}/ntos/include ${REACTOS_SOURCE_DIR}/hal/arch/common/include)
add_dependencies(hal xdk psdk bugcodes)
add_cd_file(TARGET hal DESTINATION reactos/system32 NO_CAB FOR all)
