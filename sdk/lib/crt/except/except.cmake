
if(ARCH STREQUAL "i386")
    list(APPEND LIBCNTPR_EXCEPT_ASM_SOURCE
        except/i386/chkstk_asm.s
    )
elseif(ARCH STREQUAL "amd64")
    list(APPEND LIBCNTPR_EXCEPT_SOURCE
        except/amd64/ehandler.c
    )
    list(APPEND LIBCNTPR_EXCEPT_ASM_SOURCE
        except/amd64/ehandler_asm.s
        except/amd64/chkstk_ms.s
    )
    if(NOT USE_DUMMY_PSEH)
        list(APPEND LIBCNTPR_EXCEPT_ASM_SOURCE
            except/amd64/seh.s
        )
    endif()
elseif(ARCH STREQUAL "arm")
    list(APPEND LIBCNTPR_EXCEPT_SOURCE
        except/arm/ehandler.c
    )
    list(APPEND LIBCNTPR_EXCEPT_ASM_SOURCE
        except/arm/chkstk_asm.s
        except/arm/ehandler_asm.s
    )
endif()

if(ARCH STREQUAL "i386")
    list(APPEND CHKSTK_ASM_SOURCE except/i386/chkstk_asm.s)
elseif(ARCH STREQUAL "amd64")
    list(APPEND CHKSTK_ASM_SOURCE except/amd64/chkstk_ms.s)
elseif(ARCH STREQUAL "arm")
    list(APPEND CHKSTK_ASM_SOURCE except/arm/chkstk_asm.s)
elseif(ARCH STREQUAL "arm64")
    list(APPEND CHKSTK_ASM_SOURCE except/arm64/chkstk_asm.s)
else()
    # The other targets use inline probes at each 4 KiB guard interval. There
    # is no out-of-line helper ABI and no compatibility symbols to export.
    add_library(chkstk INTERFACE)
    return()
endif()

add_asm_files(chkstk_lib_asm ${CHKSTK_ASM_SOURCE})
add_library(chkstk ${CHKSTK_SOURCE} ${chkstk_lib_asm})
if(ARCH STREQUAL "i386" AND NOT MSVC)
    target_compile_definitions(chkstk PRIVATE CRT_COMPILER_STACK_PROBE_ALIASES)
endif()
set_target_properties(chkstk PROPERTIES LINKER_LANGUAGE "C")
add_dependencies(chkstk asm)
