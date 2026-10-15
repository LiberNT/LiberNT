set(MSVCRT_DIR ${REACTOS_SOURCE_DIR}/dll/win32/msvcrt)
set(VC90_ASSEMBLY ${WINARCH}_microsoft.vc90.crt_1fc8b3b9a1e18e3b_9.0.30729.6161_none_deadbeef)

remove_definitions(-D_CRT_NON_CONFORMING_SWPRINTFS)

get_property(INCLUDE_DIRS DIRECTORY . PROPERTY INCLUDE_DIRECTORIES)
list(REMOVE_ITEM INCLUDE_DIRS "${REACTOS_SOURCE_DIR}/sdk/include/crt")
set_property(DIRECTORY . PROPERTY INCLUDE_DIRECTORIES ${INCLUDE_DIRS})
include_directories(${MSVCRT_DIR}/include ${MSVCRT_DIR})
include_directories(BEFORE ${REACTOS_SOURCE_DIR}/sdk/include/wine)

add_definitions(
    -D_CRTIMP=
    -D_MSVCR_VER=90
    -DARRAY_SIZE=_countof
    -DI64_MIN=_I64_MIN
    -DI64_MAX=_I64_MAX
    -DUI64_MAX=_UI64_MAX
    -Dx87_asin=asin
    -Dmath_error=_invoke_matherr
    -Dasm_sqrt=sqrt
    -Dasm_sqrtf=sqrtf
)

spec2def(msvcr90.dll msvcr90.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    ${MSVCRT_DIR}/console.c
    ${MSVCRT_DIR}/cpp.c
    ${MSVCRT_DIR}/ctype.c
    ${MSVCRT_DIR}/data.c
    ${MSVCRT_DIR}/dir.c
    ${MSVCRT_DIR}/environ.c
    ${MSVCRT_DIR}/errno.c
    ${MSVCRT_DIR}/except.c
    ${MSVCRT_DIR}/exit.c
    ${MSVCRT_DIR}/file.c
    ${MSVCRT_DIR}/heap.c
    ${MSVCRT_DIR}/locale.c
    ${MSVCRT_DIR}/lock.c
    ${MSVCRT_DIR}/main.c
    ${MSVCRT_DIR}/math.c
    ${MSVCRT_DIR}/mbcs.c
    ${MSVCRT_DIR}/misc.c
    ${MSVCRT_DIR}/process.c
    ${MSVCRT_DIR}/scanf.c
    ${MSVCRT_DIR}/sincos.c
    ${MSVCRT_DIR}/string.c
    ${MSVCRT_DIR}/thread.c
    ${MSVCRT_DIR}/time.c
    ${MSVCRT_DIR}/undname.c
    ${MSVCRT_DIR}/wcs.c
    ${MSVCRT_DIR}/reactos/system.cpp)

if(ARCH STREQUAL "i386")
    add_definitions(
        -D__ASM_USE_THISCALL_WRAPPER
        -DRtlUnwind=__wine_RtlUnwind)
    list(APPEND SOURCE ${MSVCRT_DIR}/except_i386.c)
    list(APPEND ASM_SOURCE
        ${MSVCRT_DIR}/i386/__wine_RtlUnwind.s
        ${MSVCRT_DIR}/i386/exception_ptr.s
        ${MSVCRT_DIR}/i386/inout.s)
elseif(ARCH STREQUAL "amd64")
    list(APPEND SOURCE ${MSVCRT_DIR}/except_x86_64.c)
elseif(ARCH STREQUAL "arm")
    list(APPEND SOURCE ${MSVCRT_DIR}/except_arm.c)
elseif(ARCH STREQUAL "arm64")
    list(APPEND SOURCE ${MSVCRT_DIR}/except_arm64.c)
elseif(ARCH STREQUAL "riscv64")
    list(APPEND SOURCE
        ${MSVCRT_DIR}/except_riscv64.c
        ${MSVCRT_DIR}/fpu_riscv64.c)
elseif(ARCH STREQUAL "ppc")
    list(APPEND SOURCE
        ${MSVCRT_DIR}/except_ppc.c
        ${MSVCRT_DIR}/fpu_ppc.c)
endif()

if(ASM_SOURCE)
    add_asm_files(msvcr90_asm ${ASM_SOURCE})
endif()

add_library(msvcr90 MODULE
    ${SOURCE}
    ${msvcr90_asm}
    msvcr90.rc
    ${CMAKE_CURRENT_BINARY_DIR}/msvcr90_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/msvcr90.def)

if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set_source_files_properties(${MSVCRT_DIR}/math.c PROPERTIES COMPILE_OPTIONS "-Wno-incompatible-library-redeclaration")
    set_property(SOURCE ${MSVCRT_DIR}/except.c APPEND PROPERTY COMPILE_OPTIONS -Wno-unused-variable -Wno-unused-function)
    set_property(SOURCE ${MSVCRT_DIR}/string.c APPEND PROPERTY COMPILE_OPTIONS -Wno-unused-function)
elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(${MSVCRT_DIR}/math.c PROPERTIES COMPILE_OPTIONS "-Wno-builtin-declaration-mismatch")
endif()

set_module_type(msvcr90 win32dll ENTRYPOINT DllMain 12)
target_link_libraries(msvcr90 crtmath dbgrpt vcruntime wine chkstk ${PSEH_LIB})
add_importlibs(msvcr90 kernel32 kernel32_vista ntdll)
add_delay_importlibs(msvcr90 user32 advapi32)
add_dependencies(msvcr90 psdk asm)
add_cd_file(TARGET msvcr90 DESTINATION reactos/winsxs/${VC90_ASSEMBLY} FOR all)
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/${VC90_ASSEMBLY}.manifest DESTINATION reactos/winsxs/manifests FOR all)

add_library(msvcr90_implib EXCLUDE_FROM_ALL
    ${MSVCRT_DIR}/iob.c
    ${MSVCRT_DIR}/mathf.c
    ${MSVCRT_DIR}/onexit.c)
add_dependencies(msvcr90_implib psdk)
target_compile_definitions(libmsvcr90 INTERFACE _DLL __USE_CRTIMP)
target_link_libraries(libmsvcr90 INTERFACE msvcr90_implib)

