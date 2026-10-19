
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs
                    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/zlib)

spec2def(mbedtls.dll mbedtls.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    _reactos_strsafe_wrapper.c
    aes.c
    aesce.c
    aesni.c
    aria.c
    asn1parse.c
    asn1write.c
    base64.c
    bignum.c
    bignum_core.c
    bignum_mod.c
    bignum_mod_raw.c
    block_cipher.c
    camellia.c
    ccm.c
    chacha20.c
    chachapoly.c
    cipher.c
    cipher_wrap.c
    cmac.c
    constant_time.c
    ctr_drbg.c
    debug.c
    des.c
    dhm.c
    ecdh.c
    ecdsa.c
    ecjpake.c
    ecp.c
    ecp_curves.c
    ecp_curves_new.c
    entropy.c
    entropy_poll.c
    error.c
    gcm.c
    hkdf.c
    hmac_drbg.c
    lmots.c
    lms.c
    md.c
    md5.c
    memory_buffer_alloc.c
    mps_reader.c
    mps_trace.c
    net_sockets.c
    nist_kw.c
    oid.c
    padlock.c
    pem.c
    pk.c
    pk_ecc.c
    pk_wrap.c
    pkcs12.c
    pkcs5.c
    pkcs7.c
    pkparse.c
    pkwrite.c
    platform.c
    platform_util.c
    poly1305.c
    psa_crypto.c
    psa_crypto_aead.c
    psa_crypto_cipher.c
    psa_crypto_client.c
    psa_crypto_driver_wrappers_no_static.c
    psa_crypto_ecp.c
    psa_crypto_ffdh.c
    psa_crypto_hash.c
    psa_crypto_mac.c
    psa_crypto_pake.c
    psa_crypto_random.c
    psa_crypto_rsa.c
    psa_crypto_se.c
    psa_crypto_slot_management.c
    psa_crypto_storage.c
    psa_its_file.c
    psa_util.c
    ripemd160.c
    rsa.c
    rsa_alt_helpers.c
    sha1.c
    sha256.c
    sha3.c
    sha512.c
    ssl_cache.c
    ssl_ciphersuites.c
    ssl_client.c
    ssl_cookie.c
    ssl_debug_helpers_generated.c
    ssl_msg.c
    ssl_ticket.c
    ssl_tls.c
    ssl_tls12_client.c
    ssl_tls12_server.c
    ssl_tls13_client.c
    ssl_tls13_generic.c
    ssl_tls13_keys.c
    ssl_tls13_server.c
    threading.c
    timing.c
    version.c
    version_features.c
    x509.c
    x509_create.c
    x509_crl.c
    x509_crt.c
    x509_csr.c
    x509write.c
    x509write_crt.c
    x509write_csr.c
    )

add_library(mbedtls MODULE
    ${SOURCE}
    mbedtls.rc
    ${CMAKE_CURRENT_BINARY_DIR}/mbedtls.def)

set_module_type(mbedtls win32dll)
target_compile_definitions(mbedtls PRIVATE _WIN32)
if(NOT MSVC)
    target_link_libraries(mbedtls libgcc)
endif()
add_importlibs(mbedtls bcrypt msvcrt kernel32 ntdll)
set_property(SOURCE x509_crt.c APPEND PROPERTY COMPILE_DEFINITIONS MBEDTLS_TEST_SW_INET_PTON)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -print-file-name=include
        OUTPUT_VARIABLE MBEDTLS_GCC_INCLUDE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    set_property(SOURCE aesni.c APPEND PROPERTY COMPILE_OPTIONS -idirafter ${MBEDTLS_GCC_INCLUDE_DIR})
endif()

# to use `_vsnprintf_s` looks like we have to define MINGW_HAS_SECURE_API
# and explicitly export it from msvcrt for it to work... what on earth?!
add_definitions(-DMINGW_HAS_SECURE_API -DCRTDLL)

add_cd_file(TARGET mbedtls DESTINATION reactos/system32 FOR all)

if(NOT MSVC)
    target_compile_options(mbedtls PRIVATE -Wno-pointer-sign -Wno-unused-function)
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(mbedtls PRIVATE -Wno-tautological-constant-compare)
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set_property(SOURCE bignum.c APPEND PROPERTY COMPILE_OPTIONS -Wno-unused-but-set-variable)
    set_property(SOURCE x509_crt.c APPEND PROPERTY COMPILE_OPTIONS -Wno-uninitialized-const-pointer)
endif()
