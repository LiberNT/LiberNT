# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif.ing@outlook.com>
#
# Compile and link flags for third-party code built against the ReactOS SDK
# headers, CRT and import libraries and the tree's matching libc++ runtime.
# reactos-sdk.cmake applies them to the nested CMake builds; sources built by
# their own configure script take them from reactos_sdk_flags().

function(reactos_sdk_flags _prefix)
    cmake_parse_arguments(_sdk "" "COMPILER;ARCH;ARCH_FLAGS;SOURCE_DIR;BUILD_DIR;LIBDIR;CXX_RUNTIME;CRT" "" ${ARGN})

    execute_process(COMMAND "${_sdk_COMPILER}" -print-resource-dir
        OUTPUT_VARIABLE _resource_dir OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(_sdk "${_sdk_SOURCE_DIR}/sdk/include")
    set(_build "${_sdk_BUILD_DIR}/sdk/include")
    set(_includes)
    foreach(_dir "${_resource_dir}/include" "${_sdk}/ucrt" "${_sdk}/vcruntime" "${_sdk}/crt" "${_sdk}/psdk"
                 "${_sdk}" "${_sdk}/ddk" "${_sdk}/reactos" "${_build}" "${_build}/psdk" "${_build}/ddk")
        string(APPEND _includes " -isystem ${_dir}")
    endforeach()
    set(_defines "-D_DLL -D_UCRT -D__USE_CRTIMP -D__REACTOS__ -D_CRT_DECLARE_NONSTDC_NAMES=1 -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00")
    set(_codegen "-fms-extensions ${_sdk_ARCH_FLAGS}")

    set(_builtins_arch "${_sdk_ARCH}")
    if(_sdk_ARCH STREQUAL "arm64ec")
        set(_builtins_arch aarch64)
    endif()
    set(_builtins "${_sdk_CXX_RUNTIME}/lib/windows/libclang_rt.builtins-${_builtins_arch}.a")
    set(_libraries "${_builtins}")
    if(_sdk_ARCH STREQUAL "arm64ec")
        list(APPEND _libraries "${_sdk_LIBDIR}/libchpe.a")
    endif()
    if(_sdk_CRT STREQUAL "full")
        list(APPEND _libraries
            "${_sdk_LIBDIR}/libucrtbase.a"
            "${_sdk_LIBDIR}/libmsvcrtex.a"
            "${_sdk_LIBDIR}/libmsvcrt.a"
            "${_sdk_LIBDIR}/libkernel32.a"
            "${_sdk_LIBDIR}/libntdll.a"
            "${_sdk_LIBDIR}/ucrtoldnames.a"
            "${_builtins}")
    elseif(_sdk_CRT STREQUAL "ucrtbase")
        list(APPEND _libraries
            "${_sdk_LIBDIR}/libucrtbase.a"
            "${_sdk_LIBDIR}/ucrtoldnames.a"
            "${_builtins}")
    elseif(NOT _sdk_CRT STREQUAL "none")
        message(FATAL_ERROR "The ReactOS SDK CRT must be full, ucrtbase or none, not ${_sdk_CRT}")
    endif()
    string(JOIN " " _libraries ${_libraries})

    set(${_prefix}_C_FLAGS "${_codegen} -nostdlibinc ${_defines}${_includes}" PARENT_SCOPE)
    set(${_prefix}_CXX_FLAGS
        "${_codegen} -nostdlibinc -nostdinc++ -isystem ${_sdk_CXX_RUNTIME}/include/c++/v1 -D__LARGE_MBSTATE_T ${_defines}${_includes}"
        PARENT_SCOPE)
    set(${_prefix}_C_LIBRARIES "${_libraries}" PARENT_SCOPE)
    set(${_prefix}_CXX_LIBRARIES
        "${_sdk_CXX_RUNTIME}/lib/libc++.a ${_sdk_CXX_RUNTIME}/lib/libc++abi.a ${_sdk_CXX_RUNTIME}/lib/libunwind.a ${_libraries}"
        PARENT_SCOPE)
    set(${_prefix}_LINK_FLAGS
        "-nostdlib -fuse-ld=lld -L${_sdk_LIBDIR} -Wl,--disable-auto-import,--disable-stdcall-fixup"
        PARENT_SCOPE)
endfunction()
