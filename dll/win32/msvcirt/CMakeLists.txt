add_definitions(-D_MSVCIRT -D__WINESRC__)

include_directories(BEFORE ${REACTOS_SOURCE_DIR}/dll/win32/msvcrt)

spec2def(msvcirt.dll msvcirt.spec)

list(APPEND SOURCE
    exception.c
    msvcirt.c
    ${CMAKE_CURRENT_BINARY_DIR}/msvcirt_stubs.c)

if(MSVC)
    if(ARCH STREQUAL "i386")
        add_definitions(-D__ASM_USE_THISCALL_WRAPPER -D__ASM_EXTERN_THISCALL_WRAPPER)
        list(APPEND ASM_SOURCE
            i386/cpp_vtables.s
            i386/thiscall.s)
    elseif(ARCH STREQUAL "amd64")
        list(APPEND ASM_SOURCE
            amd64/cpp_vtables.s)
    elseif(ARCH STREQUAL "arm64")
        list(APPEND ASM_SOURCE
            arm64/cpp_vtables.s)
    endif()
    add_asm_files(msvcirt_asm ${ASM_SOURCE})
    list(APPEND SOURCE ${msvcirt_asm})
endif()

add_library(msvcirt MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/msvcirt.def)

set_module_type(msvcirt win32dll)
target_link_libraries(msvcirt wine)
add_importlibs(msvcirt msvcrt kernel32 ntdll)

if(MSVC)
    target_link_options(msvcirt PRIVATE "/ignore:4102")
endif()

add_cd_file(TARGET msvcirt DESTINATION reactos/system32 FOR all)
