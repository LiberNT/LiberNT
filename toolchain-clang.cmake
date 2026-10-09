macro(require_llvm_program varname execname)
    if(_llvm_tool_bin_hints)
        if(DEFINED CLANG_VERSION)
            find_program(${varname} NAMES ${execname} ${execname}-${CLANG_VERSION} HINTS ${_llvm_tool_bin_hints} NO_DEFAULT_PATH)
        elseif(DEFINED LLVM_TOOL_VERSION)
            find_program(${varname} NAMES ${execname} ${execname}-${LLVM_TOOL_VERSION} HINTS ${_llvm_tool_bin_hints} NO_DEFAULT_PATH)
        else()
            find_program(${varname} NAMES ${execname} HINTS ${_llvm_tool_bin_hints} NO_DEFAULT_PATH)
        endif()
    endif()

    if(NOT ${varname})
        if(DEFINED CLANG_VERSION)
            find_program(${varname} NAMES ${execname}-${CLANG_VERSION} ${execname} HINTS ${_llvm_tool_bin_hints})
        elseif(DEFINED LLVM_TOOL_VERSION)
            find_program(${varname} NAMES ${execname}-${LLVM_TOOL_VERSION} ${execname} HINTS ${_llvm_tool_bin_hints})
        else()
            find_program(${varname} NAMES ${execname} HINTS ${_llvm_tool_bin_hints})
        endif()
    endif()

    if(NOT ${varname})
        message(FATAL_ERROR "${execname} not found")
    endif()
endmacro()

function(_detect_llvm_mingw_root _outvar)
    set(_candidate_roots)
    if(DEFINED ENV{LLVM_MINGW_ROOT})
        list(APPEND _candidate_roots "$ENV{LLVM_MINGW_ROOT}")
    endif()
    if(DEFINED ENV{LLVM_MINGW_PREFIX})
        list(APPEND _candidate_roots "$ENV{LLVM_MINGW_PREFIX}")
    endif()
    if(DEFINED ENV{HOME})
        list(APPEND _candidate_roots "$ENV{HOME}/.local/opt/rosbe/llvm")
    endif()

    list(APPEND _candidate_roots "/opt/rosbe/llvm")
    list(REMOVE_DUPLICATES _candidate_roots)

    foreach(_candidate IN LISTS _candidate_roots)
        if(EXISTS "${_candidate}/bin/clang" AND EXISTS "${_candidate}/bin/clang++")
            set(${_outvar} "${_candidate}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    set(${_outvar} "" PARENT_SCOPE)
endfunction()

set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    ARCH
    ARM64EC_RUNTIME
    CLANG_VERSION
    REACTOS_CLANG_LLVM_MINGW_ROOT)
set(CMAKE_SYSTEM_NAME Windows)

if(ARCH STREQUAL "i386")
    set(CMAKE_SYSTEM_PROCESSOR i686)
elseif(ARCH STREQUAL "amd64")
    set(CMAKE_SYSTEM_PROCESSOR x86_64)
elseif(ARCH STREQUAL "arm")
    set(CMAKE_SYSTEM_PROCESSOR arm)
elseif(ARCH STREQUAL "arm64")
    if(ARM64EC_RUNTIME)
        set(CMAKE_SYSTEM_PROCESSOR arm64ec)
    else()
        set(CMAKE_SYSTEM_PROCESSOR aarch64)
    endif()
elseif(ARCH STREQUAL "riscv64")
    set(CMAKE_SYSTEM_PROCESSOR riscv64)
elseif(ARCH STREQUAL "ppc")
    set(CMAKE_SYSTEM_PROCESSOR powerpcle)
else()
    message(FATAL_ERROR "Unsupported ARCH: ${ARCH}")
endif()

set(triplet ${CMAKE_SYSTEM_PROCESSOR}-w64-mingw32)

if(NOT DEFINED REACTOS_CLANG_LLVM_MINGW_ROOT OR NOT REACTOS_CLANG_LLVM_MINGW_ROOT)
    _detect_llvm_mingw_root(_llvm_mingw_root)
    if(_llvm_mingw_root)
        set(REACTOS_CLANG_LLVM_MINGW_ROOT "${_llvm_mingw_root}" CACHE PATH
            "RosBE LLVM toolchain root")
    endif()
endif()

set(_llvm_tool_bin_hints)
if(REACTOS_CLANG_LLVM_MINGW_ROOT)
    list(APPEND _llvm_tool_bin_hints "${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin")
endif()

require_llvm_program(CMAKE_C_COMPILER clang)
require_llvm_program(CMAKE_CXX_COMPILER clang++)
set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})

if(NOT DEFINED CLANG_VERSION)
    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -dumpversion
        OUTPUT_VARIABLE _clang_version
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX MATCH "^[0-9]+" LLVM_TOOL_VERSION "${_clang_version}")
endif()

set(CMAKE_C_COMPILER_TARGET ${triplet})
set(CMAKE_CXX_COMPILER_TARGET ${triplet})
set(CMAKE_ASM_COMPILER_TARGET ${triplet})
set(CMAKE_ASM_COMPILER_ID Clang)

set(CMAKE_MC_COMPILER native-windmc)
require_llvm_program(CMAKE_RC_COMPILER llvm-windres)
require_llvm_program(CMAKE_AR llvm-ar)
require_llvm_program(CMAKE_DLLTOOL llvm-dlltool)
require_llvm_program(CMAKE_STRIP llvm-strip)
require_llvm_program(CMAKE_OBJCOPY llvm-objcopy)
require_llvm_program(CMAKE_LINKER ld.lld)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_CREATE_STATIC_LIBRARY "<CMAKE_COMMAND> -E rm -f <TARGET>" "<CMAKE_AR> crT <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_CXX_CREATE_STATIC_LIBRARY ${CMAKE_C_CREATE_STATIC_LIBRARY})
set(CMAKE_ASM_CREATE_STATIC_LIBRARY ${CMAKE_C_CREATE_STATIC_LIBRARY})

set(CMAKE_C_STANDARD_LIBRARIES "" CACHE STRING "Standard C Libraries")
set(CMAKE_CXX_STANDARD_LIBRARIES "" CACHE STRING "Standard C++ Libraries")

set(REACTOS_CLANG_BASE_LINKER_FLAGS "-nostdlib -fuse-ld=lld --ld-path=\"${CMAKE_LINKER}\" -Wl,--enable-auto-image-base,--disable-auto-import")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${REACTOS_CLANG_BASE_LINKER_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${REACTOS_CLANG_BASE_LINKER_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${REACTOS_CLANG_BASE_LINKER_FLAGS}")

set(CMAKE_USER_MAKE_RULES_OVERRIDE "${CMAKE_CURRENT_LIST_DIR}/overrides-gcc.cmake")
