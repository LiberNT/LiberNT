# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif.ing@outlook.com>
#
# Toolchain for the nested third-party builds (Mesa, FEX) against the ReactOS
# SDK headers, the ReactOS CRT and import libraries, and the tree's matching
# libc++ runtime.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR "${REACTOS_SDK_ARCH}")
if(NOT DEFINED CMAKE_TRY_COMPILE_TARGET_TYPE)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
endif()
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    REACTOS_SDK_ARCH REACTOS_SDK_TRIPLE REACTOS_SDK_ARCH_FLAGS REACTOS_SDK_SOURCE_DIR
    REACTOS_SDK_BUILD_DIR REACTOS_SDK_LIBDIR REACTOS_SDK_CXX_RUNTIME REACTOS_SDK_CRT
    REACTOS_CLANG_LLVM_MINGW_ROOT)
if(NOT DEFINED REACTOS_SDK_CRT)
    set(REACTOS_SDK_CRT full)
endif()

set(CMAKE_C_COMPILER "${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin/clang")
set(CMAKE_CXX_COMPILER "${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin/clang++")
set(CMAKE_ASM_COMPILER "${CMAKE_C_COMPILER}")
set(CMAKE_C_COMPILER_TARGET "${REACTOS_SDK_TRIPLE}")
set(CMAKE_CXX_COMPILER_TARGET "${REACTOS_SDK_TRIPLE}")
set(CMAKE_ASM_COMPILER_TARGET "${REACTOS_SDK_TRIPLE}")
set(CMAKE_AR "${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin/llvm-ar")
set(CMAKE_RANLIB "${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin/llvm-ranlib")
set(CMAKE_LINKER "${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin/ld.lld")
set(CMAKE_RC_COMPILER "${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin/llvm-windres")
set(CMAKE_RC_FLAGS_INIT "--target=${REACTOS_SDK_TRIPLE}")
set(CMAKE_DLLTOOL "${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin/llvm-dlltool")
set(CMAKE_NM "${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin/llvm-nm")
set(CMAKE_OBJCOPY "${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin/llvm-objcopy")
set(CMAKE_STRIP "${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin/llvm-strip")

include("${CMAKE_CURRENT_LIST_DIR}/reactos-sdk-flags.cmake")
reactos_sdk_flags(_reactos_sdk
    COMPILER "${CMAKE_C_COMPILER}"
    ARCH "${REACTOS_SDK_ARCH}"
    ARCH_FLAGS "${REACTOS_SDK_ARCH_FLAGS}"
    SOURCE_DIR "${REACTOS_SDK_SOURCE_DIR}"
    BUILD_DIR "${REACTOS_SDK_BUILD_DIR}"
    LIBDIR "${REACTOS_SDK_LIBDIR}"
    CXX_RUNTIME "${REACTOS_SDK_CXX_RUNTIME}"
    CRT "${REACTOS_SDK_CRT}")

set(CMAKE_C_FLAGS "${_reactos_sdk_C_FLAGS}" CACHE STRING "C flags" FORCE)
set(CMAKE_CXX_FLAGS "${_reactos_sdk_CXX_FLAGS}" CACHE STRING "C++ flags" FORCE)
set(CMAKE_ASM_FLAGS_INIT "${REACTOS_SDK_ARCH_FLAGS}")
set(_reactos_sdk_windows_libraries)
if(REACTOS_SDK_CRT STREQUAL "full")
    set(_reactos_sdk_windows_libraries "-lkernel32 -luser32 -lgdi32 -lwinspool -lshell32 -lole32 -loleaut32 -luuid -lcomdlg32 -ladvapi32 ")
endif()
set(CMAKE_C_STANDARD_LIBRARIES "${_reactos_sdk_windows_libraries}${_reactos_sdk_C_LIBRARIES}" CACHE STRING "Standard C libraries" FORCE)
set(CMAKE_CXX_STANDARD_LIBRARIES "${_reactos_sdk_windows_libraries}${_reactos_sdk_CXX_LIBRARIES}" CACHE STRING "Standard C++ libraries" FORCE)

set(_reactos_sdk_link "${_reactos_sdk_LINK_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_reactos_sdk_link} -Wl,-entry,DllMainCRTStartup")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_reactos_sdk_link} -Wl,-entry,DllMainCRTStartup")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_reactos_sdk_link} -Wl,-entry,mainCRTStartup")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
