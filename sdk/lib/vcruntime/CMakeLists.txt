
# Replace the old CRT include directory with the UCRT include directory
get_property(INCLUDE_DIRS DIRECTORY . PROPERTY INCLUDE_DIRECTORIES)
list(REMOVE_ITEM INCLUDE_DIRS "${REACTOS_SOURCE_DIR}/sdk/include/crt")
set_property(DIRECTORY . PROPERTY INCLUDE_DIRECTORIES ${INCLUDE_DIRS})
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/ucrt)

include_directories(inc)

if(DBG)
    add_compile_definitions(_DEBUG) # TODO: define this globally
endif()

add_compile_definitions(
    _CORECRT_BUILD
    _CRT_DECLARE_GLOBAL_VARIABLES_DIRECTLY
    _NTSYSTEM_
)

if(${ARCH} STREQUAL "i386")
    list(APPEND VCRT_SETJMP_SOURCES
        i386/longjmp.c
    )
    list(APPEND VCRT_SETJMP_ASM_SOURCES
        i386/__longjmp_nounwind.s
        i386/_seh_longjmp_unwind.s
        i386/_setjmp.s
        i386/_setjmp3.s
    )
elseif(${ARCH} STREQUAL "amd64")
    list(APPEND VCRT_SETJMP_SOURCES
        amd64/longjmp.c
    )
    list(APPEND VCRT_SETJMP_ASM_SOURCES
        amd64/__longjmp_noframe.s
        amd64/_intrinsic_setjmp.s
        amd64/_setjmp.s
    )
elseif(${ARCH} STREQUAL "arm")
    list(APPEND VCRT_SETJMP_SOURCES
        arm/longjmp.c
    )
    list(APPEND VCRT_SETJMP_ASM_SOURCES
        arm/__longjmp_noframe.s
        arm/_setjmp.s
    )
elseif(${ARCH} STREQUAL "arm64" AND ARM64EC_RUNTIME)
    list(APPEND VCRT_SETJMP_SOURCES
        arm64ec/_setjmp.c
        arm64ec/longjmp.c
        arm64ec/setjmp.c
    )
elseif(${ARCH} STREQUAL "arm64")
    list(APPEND VCRT_SETJMP_SOURCES
        arm64/longjmp.c
    )
    list(APPEND VCRT_SETJMP_ASM_SOURCES
        arm64/__longjmp_noframe.s
        arm64/guardcf.S
        arm64/_setjmp.s
    )
elseif(${ARCH} STREQUAL "riscv64")
    list(APPEND VCRT_SETJMP_SOURCES
        riscv64/longjmp.c
    )
    list(APPEND VCRT_SETJMP_ASM_SOURCES
        riscv64/__longjmp_noframe.s
        riscv64/_setjmp.s
    )
elseif(${ARCH} STREQUAL "ppc")
    list(APPEND VCRT_SETJMP_SOURCES
        ppc/longjmp.c
    )
    list(APPEND VCRT_SETJMP_ASM_SOURCES
        ppc/__longjmp_noframe.s
        ppc/_setjmp.s
    )
endif()
add_asm_files(vcrt_setjmp_asm ${VCRT_SETJMP_ASM_SOURCES})

# Separate library, so we can link it to ftfd
add_library(setjmp ${VCRT_SETJMP_SOURCES} ${vcrt_setjmp_asm})
add_dependencies(setjmp psdk asm)

if(${ARCH} STREQUAL "arm64" AND NOT ARM64EC_RUNTIME)
    foreach(_lse_op CAS SWP LDADD LDCLR LDEOR LDSET)
        foreach(_lse_size 1 2 4 8 16)
            if(_lse_size EQUAL 16 AND NOT _lse_op STREQUAL "CAS")
                continue()
            endif()
            foreach(_lse_model 1 2 3 4 5)
                set(_lse_source ${CMAKE_CURRENT_BINARY_DIR}/lse/${_lse_op}${_lse_size}_${_lse_model}.S)
                configure_file(arm64/lse.S ${_lse_source} COPYONLY)
                set_source_files_properties(${_lse_source} PROPERTIES
                    COMPILE_DEFINITIONS "LSE_OP_${_lse_op};LSE_SIZE=${_lse_size};LSE_MODEL=${_lse_model}")
                list(APPEND LSE_ATOMICS_SOURCES ${_lse_source})
            endforeach()
        endforeach()
    endforeach()

    add_library(lse_atomics_um ${LSE_ATOMICS_SOURCES})
    target_compile_definitions(lse_atomics_um PRIVATE LSE_USER_MODE)
    add_library(lse_atomics_km ${LSE_ATOMICS_SOURCES})
    target_compile_definitions(lse_atomics_km PRIVATE LSE_KERNEL_MODE)
    add_library(lse_atomics_boot ${LSE_ATOMICS_SOURCES})
    add_dependencies(lse_atomics_um asm)
    add_dependencies(lse_atomics_km asm)
    add_dependencies(lse_atomics_boot asm)
endif()


if(${ARCH} STREQUAL "i386")
    list(APPEND VCRUNTIME_ASM_SOURCES
        i386/_chkesp.s
    )
elseif(${ARCH} STREQUAL "amd64")
    list(APPEND VCRUNTIME_ASM_SOURCES

    )
endif()
add_asm_files(vcruntime_asm ${VCRUNTIME_ASM_SOURCES})

list(APPEND VCRUNTIME_SOURCES
    ${vcruntime_asm}
    __std_terminate.c
    __vcrt_init.c
    purecall.cpp
)

if(${ARCH} STREQUAL "i386")
    list(APPEND VCRUNTIME_SOURCES
        i386/_chkesp_failed.c
    )
endif()

# Runtime library (linked into ucrtbase)
add_library(vcruntime ${VCRUNTIME_SOURCES} $<TARGET_OBJECTS:setjmp>)
target_link_libraries(vcruntime ${PSEH_LIB})
target_include_directories(vcruntime INTERFACE inc)

if(ARM64EC_RUNTIME)
    add_library(chpe STATIC arm64ec/chpe.S)
endif()
