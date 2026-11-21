
include_directories(include/internal/mingw-w64)

list(APPEND MSVCRTEX_SOURCE
    ${CRT_STARTUP_SOURCE}
    math/sincos.c
    math/exp2.c
    math/exp2f.c
    misc/fltused.c
    misc/isblank.c
    misc/iswblank.c
    misc/ofmt_stub.c
    stdio/acrt_iob_func.c
    stdio/ms_fwprintf.c
    stdlib/_recalloc.c
    startup/tlsatexit.c)


if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # Clang performs some optimizations requiring those funtions
    list(APPEND MSVCRTEX_SOURCE
        math/round.c
        math/roundf.c
        math/trunc.c
        math/truncf.c
        )
    if(ARCH STREQUAL "amd64")
        list(REMOVE_ITEM MSVCRTEX_SOURCE math/round.c math/roundf.c)
        list(APPEND MSVCRTEX_SOURCE math/amd64/round.c math/amd64/roundf.c)
    endif()
endif()

if(ARCH STREQUAL "i386")
    # The x86 CRT does not export modff; libc++ still needs an out-of-line copy.
    list(APPEND MSVCRTEX_SOURCE math/powf.c math/modff.c
        math/acosf.c math/asinf.c math/atan2f.c math/atanf.c math/cosf.c math/coshf.c math/expf.c
        math/fabsf.c math/fmodf.c math/log10f.c math/logf.c math/sinf.c math/sinhf.c math/tanf.c
        math/tanhf.c)
    if(MSVC)
        set_source_files_properties(math/powf.c math/modff.c
            math/acosf.c math/asinf.c math/atan2f.c math/atanf.c math/cosf.c math/coshf.c math/expf.c
            math/fabsf.c math/fmodf.c math/log10f.c math/logf.c math/sinf.c math/sinhf.c math/tanf.c
            math/tanhf.c PROPERTIES COMPILE_OPTIONS /wd4163)
    endif()
    # Clang wants __aulldiv for its optimizations
    list(APPEND MSVCRTEX_ASM_SOURCE
        except/i386/chkstk_asm.s
        except/i386/chkstk_ms.s
        math/i386/alldiv_asm.s
        math/i386/aulldiv_asm.s
        )
    if(MSVC)
        list(APPEND MSVCRTEX_ASM_SOURCE
            math/i386/alldvrm_asm.s
            math/i386/allmul_asm.s
            math/i386/allrem_asm.s
            math/i386/allshl_asm.s
            math/i386/allshr_asm.s
            math/i386/aulldvrm_asm.s
            math/i386/aullrem_asm.s
            math/i386/aullshr_asm.s
            math/i386/ftoul2_legacy_asm.s)
    endif()
    if (CMAKE_C_COMPILER_ID STREQUAL "Clang" AND NOT MSVC)
        list(APPEND MSVCRTEX_ASM_SOURCE
            math/i386/ceilf.S
            math/i386/floorf.S)
        list(APPEND MSVCRTEX_SOURCE
            math/i386/sqrtf.c)
    endif()
elseif(ARCH STREQUAL "amd64")
    list(APPEND MSVCRTEX_ASM_SOURCE
        except/amd64/chkstk_ms.s)
elseif(ARCH STREQUAL "arm")
    list(APPEND MSVCRTEX_SOURCE
        math/arm/rt_srsh.c)
    list(APPEND MSVCRTEX_ASM_SOURCE
        except/arm/chkstk_asm.s)
endif()

set_source_files_properties(${MSVCRTEX_ASM_SOURCE} PROPERTIES COMPILE_DEFINITIONS "_DLL;_MSVCRTEX_")
add_asm_files(msvcrtex_asm ${MSVCRTEX_ASM_SOURCE})

add_library(msvcrtex STATIC ${MSVCRTEX_SOURCE} ${msvcrtex_asm})
target_compile_definitions(msvcrtex PRIVATE _DLL _MSVCRTEX_)

# msvcrtex depends on msvcrt and kernel32
target_link_libraries(msvcrtex INTERFACE libmsvcrt libkernel32)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(msvcrtex PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wno-main>)
endif()
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    if(LTCG)
        target_compile_options(msvcrtex PRIVATE -fno-lto)
    endif()
endif()

set_source_files_properties(startup/crtdll.c PROPERTIES COMPILE_DEFINITIONS CRTDLL)
set_source_files_properties(startup/crtexe.c
                            startup/wcrtexe.c PROPERTIES COMPILE_DEFINITIONS _M_CEE_PURE)

target_link_libraries(msvcrtex INTERFACE oldnames)

add_dependencies(msvcrtex psdk asm)
