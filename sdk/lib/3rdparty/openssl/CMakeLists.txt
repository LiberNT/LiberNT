# PROJECT:     LiberNT Build System
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build libcrypto and libssl for the RDP server from the openssl feed
# COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>

set(OPENSSL_SOURCE_DIR "${REACTOS_SOURCE_DIR}/submodules/openssl")
if(NOT EXISTS "${OPENSSL_SOURCE_DIR}/Configure")
    message(FATAL_ERROR "OpenSSL: the openssl feed is not checked out. "
        "Run scripts/feeds update openssl, or use -DRDPSERVER_FROM_SOURCE=OFF.")
endif()

find_program(OPENSSL_PERL NAMES perl NO_CACHE)
find_program(OPENSSL_MAKE NAMES gmake make NO_CACHE)
if(NOT OPENSSL_PERL OR NOT OPENSSL_MAKE)
    message(FATAL_ERROR "OpenSSL needs perl and make to configure. Use -DRDPSERVER_FROM_SOURCE=OFF.")
endif()

if(ARCH STREQUAL "i386")
    set(_openssl_target mingw)
else()
    set(_openssl_target mingw64)
endif()

set(_openssl_options
    no-asm no-shared no-module no-dso no-engine no-autoload-config
    no-apps no-tests no-docs no-uplink
    no-aria no-bf no-camellia no-cast no-idea no-mdc2 no-rc2 no-rmd160 no-seed
    no-siphash no-sm2 no-sm3 no-sm4 no-whirlpool
    no-ec2m no-gost no-ml-dsa no-ml-kem no-slh-dsa no-dsa
    no-cmp no-cms no-ct no-http no-ocsp no-rfc3779 no-srp no-ts
    no-async no-comp no-dgram no-dtls no-filenames no-nextprotoneg no-ocb no-psk
    no-quic no-scrypt no-siv no-srtp no-ssl3 no-ui-console no-weak-ssl-ciphers)

set(_openssl_config "${CMAKE_CURRENT_BINARY_DIR}/config")
execute_process(
    COMMAND git -C "${OPENSSL_SOURCE_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE _openssl_revision
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
string(SHA256 _openssl_key "${_openssl_revision};${_openssl_target};${_openssl_options}")
set(_openssl_stamp "${_openssl_config}/.libernt-config")
set(_openssl_configured "")
if(EXISTS "${_openssl_stamp}")
    file(READ "${_openssl_stamp}" _openssl_configured)
endif()
if(NOT _openssl_configured STREQUAL _openssl_key)
    message(STATUS "OpenSSL: configuring ${_openssl_target}")
    file(REMOVE_RECURSE "${_openssl_config}")
    file(MAKE_DIRECTORY "${_openssl_config}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env SOURCE_DATE_EPOCH=0
            "${OPENSSL_PERL}" "${OPENSSL_SOURCE_DIR}/Configure" ${_openssl_target} ${_openssl_options}
        WORKING_DIRECTORY "${_openssl_config}"
        RESULT_VARIABLE _openssl_result
        OUTPUT_VARIABLE _openssl_output
        ERROR_VARIABLE _openssl_output)
    if(NOT _openssl_result EQUAL 0)
        message(FATAL_ERROR "OpenSSL: Configure failed:\n${_openssl_output}")
    endif()
endif()

foreach(_openssl_library generated libcrypto libssl)
    execute_process(
        COMMAND "${OPENSSL_PERL}" "${CMAKE_CURRENT_SOURCE_DIR}/sources.pl" "${_openssl_config}" ${_openssl_library}
        RESULT_VARIABLE _openssl_result
        OUTPUT_VARIABLE _openssl_sources
        ERROR_VARIABLE _openssl_error)
    if(NOT _openssl_result EQUAL 0)
        message(FATAL_ERROR "OpenSSL: listing the ${_openssl_library} sources failed: ${_openssl_error}")
    endif()
    string(REGEX REPLACE "\n$" "" _openssl_sources "${_openssl_sources}")
    string(REPLACE "\n" ";" _openssl_sources "${_openssl_sources}")
    if(_openssl_library STREQUAL "generated")
        set(_openssl_generated "${_openssl_sources}")
        continue()
    endif()
    set(_openssl_${_openssl_library}_sources)
    foreach(_openssl_source ${_openssl_sources})
        get_filename_component(_openssl_source "${_openssl_source}" ABSOLUTE BASE_DIR "${_openssl_config}")
        list(APPEND _openssl_${_openssl_library}_sources "${_openssl_source}")
    endforeach()
endforeach()
if(NOT _openssl_configured STREQUAL _openssl_key)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env SOURCE_DATE_EPOCH=0
            "${OPENSSL_MAKE}" ${_openssl_generated}
        WORKING_DIRECTORY "${_openssl_config}"
        RESULT_VARIABLE _openssl_result
        OUTPUT_VARIABLE _openssl_output
        ERROR_VARIABLE _openssl_output)
    if(NOT _openssl_result EQUAL 0)
        message(FATAL_ERROR "OpenSSL: generating the configured files failed:\n${_openssl_output}")
    endif()
    file(WRITE "${_openssl_stamp}" "${_openssl_key}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${OPENSSL_SOURCE_DIR}/VERSION.dat" "${CMAKE_CURRENT_SOURCE_DIR}/sources.pl")

add_definitions(
    -DL_ENDIAN
    -DOPENSSL_PIC
    -DOPENSSL_BUILDING_OPENSSL
    -DSTATIC_LEGACY
    -DUNICODE
    -D_UNICODE
    -DWIN32_LEAN_AND_MEAN
    -D_MT
    -DNDEBUG
    -DWIN32
    -D_WINDOWS
    -D_REENTRANT
    -D_CRT_DECLARE_NONSTDC_NAMES=1
    -D_CRT_NONSTDC_NO_DEPRECATE
    -D_DLL -D__USE_CRTIMP)

include_directories(
    ${_openssl_config}
    ${_openssl_config}/include
    ${_openssl_config}/crypto
    ${_openssl_config}/providers/common/include
    ${_openssl_config}/providers/common/include/prov
    ${OPENSSL_SOURCE_DIR}
    ${OPENSSL_SOURCE_DIR}/include
    ${OPENSSL_SOURCE_DIR}/crypto
    ${OPENSSL_SOURCE_DIR}/crypto/modes
    ${OPENSSL_SOURCE_DIR}/providers/common/include
    ${OPENSSL_SOURCE_DIR}/providers/common/include/prov
    ${OPENSSL_SOURCE_DIR}/providers/implementations/include
    ${OPENSSL_SOURCE_DIR}/providers/fips/include)

add_library(libcrypto ${_openssl_libcrypto_sources})
add_library(libssl ${_openssl_libssl_sources})

foreach(_target libcrypto libssl)
    if(NOT MSVC)
        target_compile_options(${_target} PRIVATE -w)
    endif()
    if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
        target_compile_options(${_target} PRIVATE -fno-autolink)
    endif()
    target_include_directories(${_target} SYSTEM PUBLIC ${_openssl_config}/include ${OPENSSL_SOURCE_DIR}/include)
    target_compile_definitions(${_target} PRIVATE
        [[OPENSSLDIR="C:/Program Files/Common Files/SSL"]]
        [[ENGINESDIR=""]]
        [[MODULESDIR=""]])
    add_dependencies(${_target} psdk)
endforeach()

target_link_libraries(libssl libcrypto)
