
spec2def(vssapi.dll vssapi.spec)

list(APPEND SOURCE
    main.c
    ${CMAKE_CURRENT_BINARY_DIR}/vssapi_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/vssapi.def)

if(MSVC AND (ARCH STREQUAL "i386"))
    add_definitions(-D__ASM_EXTERN_THISCALL_WRAPPER)
    add_asm_files(vssapi_asm i386/thiscall.s)
    list(APPEND SOURCE ${vssapi_asm})
endif()

add_library(vssapi MODULE ${SOURCE})
set_module_type(vssapi win32dll)
target_link_libraries(vssapi wine wine_dll_canunload)
add_importlibs(vssapi msvcrt kernel32 ntdll)
add_cd_file(TARGET vssapi DESTINATION reactos/system32 FOR all)
set_wine_module(vssapi)
