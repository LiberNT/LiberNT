# PROJECT:     LiberNT Build System
# PURPOSE:     Optional RV64X compiler (dll/win32/amd64emu/RV64X.md)
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

set(RV64X_CLANG "" CACHE FILEPATH "Clang with the RV64X pass (submodules/llvm-rv64x.patch)")
unset(RV64X_COMPILER_LAUNCHER CACHE)
unset(RV64X_COMPILER_LAUNCHER)
if(RV64X_CLANG AND ENABLE_FELIX86)
    execute_process(COMMAND ${CMAKE_C_COMPILER} -print-resource-dir
                    OUTPUT_VARIABLE _rv64x_resource_dir OUTPUT_STRIP_TRAILING_WHITESPACE)
    file(WRITE ${CMAKE_BINARY_DIR}/rv64x-cc
         "#!/bin/sh\nshift\nexec \"${RV64X_CLANG}\" -resource-dir \"${_rv64x_resource_dir}\" \"$@\"\n")
    file(CHMOD ${CMAKE_BINARY_DIR}/rv64x-cc PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
         WORLD_READ WORLD_EXECUTE)
    set(RV64X_COMPILER_LAUNCHER ${CMAKE_BINARY_DIR}/rv64x-cc)
endif()

option(RV64X_SYSTEM "Build every user-mode module of a riscv64 build as RV64X; needs RV64X_CLANG" OFF)
if(RV64X_COMPILER_LAUNCHER AND RV64X_SYSTEM)
    set(CMAKE_C_COMPILER_LAUNCHER ${RV64X_COMPILER_LAUNCHER})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${RV64X_COMPILER_LAUNCHER})
    add_compile_options("$<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<NOT:$<BOOL:$<TARGET_PROPERTY:RV64X_EXCLUDE>>>>:SHELL:-mllvm -riscv-rv64x>")
endif()
