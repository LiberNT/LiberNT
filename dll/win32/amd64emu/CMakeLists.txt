
include(${REACTOS_SOURCE_DIR}/submodules/felix86.cmake)
if(NOT FELIX86_AVAILABLE)
    return()
endif()

set(_felix86_adapter ${CMAKE_CURRENT_SOURCE_DIR}/felix86)

add_library(felix86_core OBJECT
    ${FELIX86_CORE_SOURCES}
    ${FELIX86_BISCUIT_SOURCES}
    felix86/core.cpp
    felix86/runtime.cpp)
target_include_directories(felix86_core BEFORE PRIVATE
    ${_felix86_adapter}/override
    ${_felix86_adapter}/compat
    ${FELIX86_INCLUDE_DIRECTORIES}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_felix86_adapter})
target_compile_definitions(felix86_core PRIVATE
    SOURCE_PATH_SIZE=${FELIX86_SOURCE_PATH_SIZE}
    FELIX86_NT_VERSION="felix86 ${FELIX86_VERSION}"
    FMT_HEADER_ONLY=1
    FMT_USE_LOCALE=0
    FMT_USE_EXCEPTIONS=0
    _USE_MATH_DEFINES
    _CRT_DECLARE_NONSTDC_NAMES=1
    _CRT_NONSTDC_NO_DEPRECATE
    ${FELIX86_ZYDIS_DEFINITIONS}
    ${FELIX86_SOFTFLOAT_DEFINITIONS})
target_compile_options(felix86_core PRIVATE
    -O2
    "SHELL:-include ${_felix86_adapter}/compat/felix86_nt.h"
    "SHELL:-include ${FELIX86_SOURCE_DIR}/src/felix86/common/assert_override.hpp"
    -Wno-format
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-invalid-offsetof
    -Wno-unknown-attributes
    -Wno-c99-designator
    -Wno-pragma-clang-attribute)
set_target_properties(felix86_core PROPERTIES CXX_STANDARD 20 CXX_EXTENSIONS ON)

add_library(felix86_zydis OBJECT ${FELIX86_ZYDIS_SOURCES})
target_include_directories(felix86_zydis PRIVATE ${FELIX86_INCLUDE_DIRECTORIES})
target_compile_definitions(felix86_zydis PRIVATE
    ${FELIX86_ZYDIS_DEFINITIONS}
    ZYAN_NO_LIBC
    ZYDIS_DISABLE_ENCODER
    ZYDIS_DISABLE_AVX512
    ZYDIS_DISABLE_KNC)
target_compile_options(felix86_zydis PRIVATE -O2 -Wno-unused-function)

add_library(felix86_softfloat OBJECT ${FELIX86_SOFTFLOAT_SOURCES})
target_include_directories(felix86_softfloat PRIVATE ${FELIX86_INCLUDE_DIRECTORIES})
target_compile_definitions(felix86_softfloat PRIVATE ${FELIX86_SOFTFLOAT_DEFINITIONS} INLINE=inline)
target_compile_options(felix86_softfloat PRIVATE
    -O2
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-parentheses
    -Wno-implicit-fallthrough)

add_library(felix86_cephes OBJECT ${FELIX86_CEPHES_SOURCES})
target_include_directories(felix86_cephes PRIVATE ${FELIX86_INCLUDE_DIRECTORIES})
target_compile_definitions(felix86_cephes PRIVATE ${FELIX86_SOFTFLOAT_DEFINITIONS})
target_compile_options(felix86_cephes PRIVATE
    -O2
    -fno-builtin
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-parentheses)

spec2def(amd64emu.dll amd64emu.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    exception.c
    image.c
    process.c
    redirect.c
    syscall.c
    thread.c
    felix86/cpu.c
    felix86/ntsupport.c
    signature.cpp
    amd64emu.h)

add_library(amd64emu MODULE
    ${SOURCE}
    invoke.S
    felix86/support.S
    ${REACTOS_SOURCE_DIR}/sdk/lib/crt/math/expm1.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/crt/math/log1p.c
    $<TARGET_OBJECTS:felix86_core>
    $<TARGET_OBJECTS:felix86_zydis>
    $<TARGET_OBJECTS:felix86_softfloat>
    $<TARGET_OBJECTS:felix86_cephes>
    ${CMAKE_CURRENT_BINARY_DIR}/amd64emu_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/amd64emu.def)

set_module_type(amd64emu win32dll ENTRYPOINT 0 IMAGEBASE 0x3d00000000)
target_include_directories(amd64emu BEFORE PRIVATE ${_felix86_adapter}/compat)
target_include_directories(amd64emu PRIVATE
    ${REACTOS_SOURCE_DIR}/ntoskrnl/include
    ${REACTOS_SOURCE_DIR}/win32ss
    ${REACTOS_SOURCE_DIR}/sdk/lib/crt/math)
target_compile_definitions(amd64emu PRIVATE
    NTDDI_VERSION=${REACTOS_TARGET_NTDDI}
    _CRT_DECLARE_NONSTDC_NAMES=1
    _CRT_NONSTDC_NO_DEPRECATE)
target_compile_options(amd64emu PRIVATE -Wno-microsoft-anon-tag -Wno-unused-function)
target_link_libraries(amd64emu
    $<TARGET_FILE:libstdc++>
    $<TARGET_FILE:libsupc++>
    crtmath
    libcntpr
    ${PSEH_LIB})
set_target_properties(amd64emu PROPERTIES CXX_STANDARD 20 CXX_EXTENSIONS ON)
add_importlibs(amd64emu ntdll)
add_cd_file(TARGET amd64emu DESTINATION reactos/system32 FOR all)

add_executable(amd64host host.c)
set_module_type(amd64host nativecui ENTRYPOINT EmuHostEntry IMAGEBASE 0x3d10000000)
target_link_options(amd64host PRIVATE -Wl,--stack,0x100000)
add_importlibs(amd64host amd64emu ntdll)
add_cd_file(TARGET amd64host DESTINATION reactos/system32 FOR all)
