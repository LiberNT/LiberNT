# PROJECT:     LiberNT Build System
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build Rust crates as static libraries linked into OS modules
# COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>

if(MSVC)
    if(ARCH STREQUAL "amd64")
        set(RUST_TARGET x86_64-pc-windows-msvc)
    elseif(ARCH STREQUAL "i386")
        set(RUST_TARGET i686-pc-windows-msvc-pentium)
        set(RUST_TARGET_SPEC "${REACTOS_SOURCE_DIR}/sdk/cmake/rust-targets/${RUST_TARGET}.json")
    elseif(ARCH STREQUAL "arm64")
        if(ARM64EC_RUNTIME)
            set(RUST_TARGET arm64ec-pc-windows-msvc)
        else()
            set(RUST_TARGET aarch64-pc-windows-msvc)
        endif()
    else()
        message(FATAL_ERROR "Rust is not supported on ${ARCH} with MSVC.")
    endif()
elseif(ARCH STREQUAL "amd64")
    set(RUST_TARGET x86_64-pc-windows-gnullvm)
elseif(ARCH STREQUAL "i386")
    set(RUST_TARGET i686-pc-windows-gnullvm-pentium)
    set(RUST_TARGET_SPEC "${REACTOS_SOURCE_DIR}/sdk/cmake/rust-targets/${RUST_TARGET}.json")
elseif(ARCH STREQUAL "arm64")
    set(RUST_TARGET aarch64-pc-windows-gnullvm)
elseif(ARCH STREQUAL "riscv64")
    set(RUST_TARGET riscv64-pc-windows-gnullvm)
    set(RUST_TARGET_SPEC "${REACTOS_SOURCE_DIR}/sdk/cmake/rust-targets/${RUST_TARGET}.json")
    set(RUST_COFF_VIA_CLANG TRUE)
    set(RUST_IR_TRIPLE riscv64-unknown-elf)
    set(RUST_COFF_FLAGS "-mabi=lp64 -mcmodel=medany -mno-relax")
elseif(ARCH STREQUAL "ppc")
    set(RUST_TARGET powerpcle-pc-windows-gnullvm)
    set(RUST_TARGET_SPEC "${REACTOS_SOURCE_DIR}/sdk/cmake/rust-targets/${RUST_TARGET}.json")
    set(RUST_COFF_VIA_CLANG TRUE)
    set(RUST_IR_TRIPLE powerpcle-unknown-elf)
    set(RUST_COFF_FLAGS "-mcpu=${OARCH}")
else()
    message(FATAL_ERROR "Rust is not supported on ${ARCH}.")
endif()

if(RUST_COFF_VIA_CLANG AND NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "Rust on ${ARCH} writes COFF through the Clang toolchain; configure with --clang.")
endif()

find_program(CARGO_EXECUTABLE cargo HINTS "$ENV{CARGO_HOME}/bin" "$ENV{HOME}/.cargo/bin")
get_filename_component(_rust_bin_dir "${CARGO_EXECUTABLE}" DIRECTORY)
find_program(RUSTC_EXECUTABLE rustc HINTS "${_rust_bin_dir}" NO_DEFAULT_PATH)
if(NOT CARGO_EXECUTABLE OR NOT RUSTC_EXECUTABLE)
    message(FATAL_ERROR "The OS needs Rust but cargo or rustc was not found. "
        "Install Rust with rustup or set CARGO_EXECUTABLE.")
endif()

set(RUST_TARGET_DIR "${REACTOS_BINARY_DIR}/rust")

execute_process(
    COMMAND "${RUSTC_EXECUTABLE}" --print sysroot
    OUTPUT_VARIABLE _rust_sysroot
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT EXISTS "${_rust_sysroot}/lib/rustlib/src/rust/library/core/Cargo.toml")
    message(FATAL_ERROR "${RUSTC_EXECUTABLE} has no library sources to build core for ${RUST_TARGET}. "
        "Install them with: rustup component add rust-src")
endif()

set(_rust_fetch
    ${CMAKE_COMMAND} -E env "RUSTC=${RUSTC_EXECUTABLE}" RUSTC_BOOTSTRAP=1
    "${CARGO_EXECUTABLE}" fetch --locked --quiet
    --manifest-path "${_rust_sysroot}/lib/rustlib/src/rust/library/Cargo.toml")
execute_process(COMMAND ${_rust_fetch} --offline RESULT_VARIABLE _rust_fetch_result OUTPUT_QUIET ERROR_QUIET)
if(_rust_fetch_result)
    execute_process(COMMAND ${_rust_fetch} RESULT_VARIABLE _rust_fetch_result)
    if(_rust_fetch_result)
        message(FATAL_ERROR "Cannot download the crates that -Zbuild-std needs for ${RUST_TARGET}.")
    endif()
endif()

if(RUST_COFF_VIA_CLANG)
    file(MAKE_DIRECTORY "${RUST_TARGET_DIR}")
    file(WRITE "${RUST_TARGET_DIR}/layout.c" "")
    execute_process(
        COMMAND "${CMAKE_C_COMPILER}" --target=${CMAKE_C_COMPILER_TARGET} -S -emit-llvm -o - "${RUST_TARGET_DIR}/layout.c"
        OUTPUT_VARIABLE _rust_coff_ir
        ERROR_QUIET)
    string(REGEX MATCH "target datalayout = \"([^\"]*)\"" _ "${_rust_coff_ir}")
    set(RUST_COFF_LAYOUT "${CMAKE_MATCH_1}")
    string(REGEX MATCH "target triple = \"([^\"]*)\"" _ "${_rust_coff_ir}")
    set(RUST_COFF_TRIPLE "${CMAKE_MATCH_1}")
    if(NOT RUST_COFF_LAYOUT OR NOT RUST_COFF_TRIPLE)
        message(FATAL_ERROR "${CMAKE_C_COMPILER} did not report the data layout of ${CMAKE_C_COMPILER_TARGET}.")
    endif()
endif()

# add_rust_library(<name> [<crate directory>])
function(add_rust_library _name)
    if(ARGC GREATER 1)
        set(_crate_dir "${ARGV1}")
    else()
        set(_crate_dir "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()

    set(_out_dir "${RUST_TARGET_DIR}/${RUST_TARGET}/release")
    if(MSVC)
        set(_cargo_lib "${_out_dir}/${_name}.lib")
    else()
        set(_cargo_lib "${_out_dir}/lib${_name}.a")
    endif()
    set(_rustflags "'-Dwarnings'")
    if(RUST_TARGET_SPEC)
        set(_target -Zjson-target-spec --target "${RUST_TARGET_SPEC}")
    else()
        set(_target --target ${RUST_TARGET})
    endif()
    if(RUST_COFF_VIA_CLANG)
        string(APPEND _rustflags ", '-Clinker-plugin-lto'")
    endif()

    file(GLOB_RECURSE _crate_sources CONFIGURE_DEPENDS "${_crate_dir}/src/*.rs")

    add_custom_command(
        OUTPUT "${_cargo_lib}"
        COMMAND ${CMAKE_COMMAND} -E env
            "RUSTC=${RUSTC_EXECUTABLE}"
            "CARGO_TARGET_DIR=${RUST_TARGET_DIR}"
            "RUSTC_BOOTSTRAP=1"
            "${CARGO_EXECUTABLE}" build --release --locked --offline --quiet
            --manifest-path "${_crate_dir}/Cargo.toml"
            -Zbuild-std=core,alloc ${_target}
            --config "profile.release.panic='abort'"
            --config "profile.release.debug=1"
            --config "build.rustflags=[${_rustflags}]"
        DEPENDS "${_crate_dir}/Cargo.toml" "${_crate_dir}/Cargo.lock" ${_crate_sources}
        COMMENT "Building Rust crate ${_name}"
        VERBATIM)

    set(_lib "${_cargo_lib}")
    if(RUST_COFF_VIA_CLANG)
        set(_lib "${_out_dir}/lib${_name}.coff.a")
        add_custom_command(
            OUTPUT "${_lib}"
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${_cargo_lib}"
                "-DOUTPUT=${_lib}"
                "-DWORK_DIR=${_out_dir}/${_name}.coff"
                "-DCLANG=${CMAKE_C_COMPILER}"
                "-DAR=${CMAKE_AR}"
                "-DIR_TRIPLE=${RUST_IR_TRIPLE}"
                "-DCOFF_TRIPLE=${RUST_COFF_TRIPLE}"
                "-DCOFF_LAYOUT=${RUST_COFF_LAYOUT}"
                "-DCOFF_FLAGS=${RUST_COFF_FLAGS}"
                -P "${REACTOS_SOURCE_DIR}/sdk/cmake/rust-coff.cmake"
            DEPENDS "${_cargo_lib}" "${REACTOS_SOURCE_DIR}/sdk/cmake/rust-coff.cmake"
            COMMENT "Compiling Rust crate ${_name} for ${ARCH}"
            VERBATIM)
    endif()

    set(_checks)
    if(CMAKE_NM)
        set(_checks "${_lib}.nopanic")
        add_custom_command(
            OUTPUT "${_checks}"
            COMMAND ${CMAKE_COMMAND}
                "-DNM=${CMAKE_NM}"
                "-DLIBRARY=${_lib}"
                "-DCRATE=${_name}"
                "-DSTAMP=${_checks}"
                -P "${REACTOS_SOURCE_DIR}/sdk/cmake/rust-nopanic.cmake"
            DEPENDS "${_lib}" "${REACTOS_SOURCE_DIR}/sdk/cmake/rust-nopanic.cmake"
            COMMENT "Checking Rust crate ${_name} for panics"
            VERBATIM)
    endif()
    add_custom_target(${_name}_cargo DEPENDS "${_lib}" ${_checks})

    add_library(${_name} STATIC IMPORTED GLOBAL)
    set_target_properties(${_name} PROPERTIES IMPORTED_LOCATION "${_lib}")
    add_dependencies(${_name} ${_name}_cargo)
endfunction()
