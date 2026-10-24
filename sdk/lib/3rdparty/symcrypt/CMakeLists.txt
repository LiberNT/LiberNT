list(APPEND SOURCE
    lib/3des.c
    lib/AesTables.c
    lib/DesTables.c
    lib/FatalIntercept.c
    lib/IEEE802_11SaeCustom.c
    lib/ScsTable.c
    lib/a_dispatch.c
    lib/aes-asm.c
    lib/aes-c.c
    lib/aes-default-bc.c
    lib/aes-default.c
    lib/aes-key.c
    lib/aes-neon.c
    lib/aes-xmm.c
    lib/aes-ymm.c
    lib/aesCtrDrbg.c
    lib/aescmac.c
    lib/aeskw.c
    lib/blockciphermodes.c
    lib/ccm.c
    lib/chacha20.c
    lib/chacha20_poly1305.c
    lib/cpuid.c
    lib/cpuid_um.c
    lib/crt.c
    lib/desx.c
    lib/dh.c
    lib/dl_internal_groups.c
    lib/dlgroup.c
    lib/dlkey.c
    lib/dsa.c
    lib/ec_dh.c
    lib/ec_dispatch.c
    lib/ec_dsa.c
    lib/ec_internal_curve_params.c
    lib/ec_internal_curves.c
    lib/ec_montgomery.c
    lib/ec_mul.c
    lib/ec_short_weierstrass.c
    lib/ec_twisted_edwards.c
    lib/eckey.c
    lib/ecpoint.c
    lib/ecurve.c
    lib/env_windowsUserModeWin8_1.c
    lib/equal.c
    lib/fdef369_mod.c
    lib/fdef_general.c
    lib/fdef_int.c
    lib/fdef_mod.c
    lib/gcm.c
    lib/gen_int.c
    lib/ghash.c
    lib/hash.c
    lib/hkdf.c
    lib/hmac.c
    lib/hmacmd5.c
    lib/hmacsha1.c
    lib/hmacsha224.c
    lib/hmacsha256.c
    lib/hmacsha384.c
    lib/hmacsha3_224.c
    lib/hmacsha3_256.c
    lib/hmacsha3_384.c
    lib/hmacsha3_512.c
    lib/hmacsha512.c
    lib/hmacsha512_224.c
    lib/hmacsha512_256.c
    lib/kmac.c
    lib/libmain.c
    lib/lms.c
    lib/marvin32.c
    lib/md2.c
    lib/md4.c
    lib/md5.c
    lib/mldsa.c
    lib/mldsa_primitives.c
    lib/mlkem.c
    lib/mlkem_primitives.c
    lib/modexp.c
    lib/paddingPkcs7.c
    lib/pbkdf2.c
    lib/pbkdf2_hmacsha1.c
    lib/pbkdf2_hmacsha256.c
    lib/poly1305.c
    lib/primes.c
    lib/rc2.c
    lib/rc4.c
    lib/rdrand.c
    lib/rdseed.c
    lib/recoding.c
    lib/rsa_enc.c
    lib/rsa_padding.c
    lib/rsakey.c
    lib/scsTools.c
    lib/selftest.c
    lib/session.c
    lib/sha1.c
    lib/sha256-xmm.c
    lib/sha256.c
    lib/sha3.c
    lib/sha3_224.c
    lib/sha3_256.c
    lib/sha3_384.c
    lib/sha3_512.c
    lib/sha512.c
    lib/shake.c
    lib/sp800_108.c
    lib/sp800_108_hmacsha1.c
    lib/sp800_108_hmacsha256.c
    lib/sp800_108_hmacsha512.c
    lib/srtp_kdf.c
    lib/ssh_kdf.c
    lib/ssh_kdf_sha256.c
    lib/ssh_kdf_sha512.c
    lib/sskdf.c
    lib/tlsCbcVerify.c
    lib/tlsprf.c
    lib/xmss.c
    lib/xtsaes.c)

if(ARCH STREQUAL "i386")
    list(APPEND SOURCE
        lib/i386/aesasm.S
        lib/i386/fdef_asm.S
        lib/i386/wipe.S)
elseif(ARCH STREQUAL "amd64")
    list(APPEND SOURCE
        lib/amd64/aesasm.S
        lib/amd64/fdef369_asm.S
        lib/amd64/fdef_asm.S
        lib/amd64/fdef_mulx.S
        lib/amd64/sha256xmm_asm.S
        lib/amd64/wipe.S)
elseif(ARCH STREQUAL "arm")
    list(APPEND SOURCE
        lib/arm/aesasm.S
        lib/arm/fdef_asm.S
        lib/arm/wipe.S)
elseif(ARCH STREQUAL "arm64" AND ARM64EC_RUNTIME)
    list(APPEND SOURCE
        lib/arm64ec/fdef369_asm.S
        lib/arm64ec/fdef_asm.S
        lib/arm64ec/wipe.S)
elseif(ARCH STREQUAL "arm64" AND MSVC)
    include_directories(inc lib)
    set_source_files_properties(lib/arm64/fdef369_asm.S lib/arm64/fdef_asm.S lib/arm64/wipe.S
        PROPERTIES COMPILE_DEFINITIONS SYMCRYPT_PLATFORM_WINE=1)
    add_asm_files(symcrypt_arm64_asm
        lib/arm64/fdef369_asm.S
        lib/arm64/fdef_asm.S
        lib/arm64/wipe.S)
    list(APPEND SOURCE ${symcrypt_arm64_asm})
elseif(ARCH STREQUAL "arm64")
    list(APPEND SOURCE
        lib/arm64/fdef369_asm.S
        lib/arm64/fdef_asm.S
        lib/arm64/wipe.S)
endif()

if(MSVC AND ARCH STREQUAL "i386")
    set(SYMCRYPT_MASM_SOURCE
        lib/i386/aesasm.cppasm
        lib/i386/fdef_asm.cppasm
        lib/i386/wipe.asm)
    set(SYMCRYPT_MASM_DEPENDS
        inc/C_asm_shared.inc
        inc/symcrypt_internal_shared.inc
        lib/i386/symcrypt_magic.inc)
    set(SYMCRYPT_MASM_CPU X86)
    set(SYMCRYPT_MASM_FLAGS /safeseh)
elseif(MSVC AND ARCH STREQUAL "amd64")
    find_program(SYMCRYPT_PYTHON_EXECUTABLE NAMES python3 python REQUIRED NO_CMAKE_FIND_ROOT_PATH)
    set(SYMCRYPT_MASM_SOURCE
        lib/amd64/aesasm.symcryptasm
        lib/amd64/fdef369_asm.symcryptasm
        lib/amd64/fdef_asm.symcryptasm
        lib/amd64/fdef_mulx.symcryptasm
        lib/amd64/sha256xmm_asm.symcryptasm
        lib/amd64/wipe.symcryptasm)
    set(SYMCRYPT_MASM_DEPENDS
        inc/C_asm_shared.inc
        inc/symcrypt_internal_shared.inc
        lib/symcryptasm_shared.cppasm
        lib/amd64/sha2common_asm.symcryptasm
        scripts/symcryptasm_processor.py)
    set(SYMCRYPT_MASM_CPU AMD64)
    set(SYMCRYPT_MASM_FLAGS /I${REACTOS_SOURCE_DIR}/sdk/lib/ucrt/inc)
endif()

foreach(_file ${SYMCRYPT_MASM_SOURCE})
    get_filename_component(_name ${_file} NAME_WE)
    get_filename_component(_ext ${_file} EXT)
    set(_input ${CMAKE_CURRENT_SOURCE_DIR}/${_file})
    set(_output ${CMAKE_CURRENT_BINARY_DIR}/masm/${_name})
    if(_ext STREQUAL ".symcryptasm")
        add_custom_command(
            OUTPUT ${_output}.cppasm
            COMMAND ${SYMCRYPT_PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/symcryptasm_processor.py masm amd64 msft ${_input} ${_output}.cppasm
            DEPENDS ${_input} ${SYMCRYPT_MASM_DEPENDS}
            VERBATIM)
        set(_input ${_output}.cppasm)
    endif()
    if(NOT _ext STREQUAL ".asm")
        add_custom_command(
            OUTPUT ${_output}.asm
            COMMAND ${CMAKE_C_COMPILER} /nologo /X /EP /P /Fi${_output}.asm
                /I${CMAKE_CURRENT_SOURCE_DIR}/inc /I${CMAKE_CURRENT_SOURCE_DIR}/lib
                /DSYMCRYPT_MASM /DSYMCRYPT_CPU_${SYMCRYPT_MASM_CPU} /DDBG=$<BOOL:${DBG}> ${_input}
            DEPENDS ${_input} ${SYMCRYPT_MASM_DEPENDS}
            VERBATIM)
        set(_input ${_output}.asm)
    endif()
    add_custom_command(
        OUTPUT ${_output}.obj
        COMMAND ${CMAKE_ASM_MASM_COMPILER} /nologo ${_quiet_flag} /X ${SYMCRYPT_MASM_FLAGS} /Fo${_output}.obj /c /Ta ${_input}
        DEPENDS ${_input}
        VERBATIM)
    list(APPEND SOURCE ${_output}.obj)
endforeach()

set(SYMCRYPT_USER_SOURCE ${SOURCE})
set(SYMCRYPT_KERNEL_SOURCE ${SOURCE})
list(REMOVE_ITEM SYMCRYPT_KERNEL_SOURCE
    lib/env_windowsUserModeWin8_1.c
    lib/cpuid_um.c)

add_library(symcrypt STATIC ${SYMCRYPT_USER_SOURCE})
add_dependencies(symcrypt psdk)
add_library(symcrypt_km STATIC ${SYMCRYPT_KERNEL_SOURCE})
add_dependencies(symcrypt_km psdk)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(lib/AesTables.c lib/DesTables.c PROPERTIES COMPILE_OPTIONS "-Wno-missing-braces")
endif()
target_compile_definitions(symcrypt PRIVATE
    __WINE_PE_BUILD
    SYMCRYPT_PLATFORM_WINE=1)
target_compile_definitions(symcrypt_km PRIVATE
    __WINE_PE_BUILD
    SYMCRYPT_PLATFORM_WINE=1
    _KERNEL_MODE=1)
if(NOT ARCH MATCHES "^(i386|amd64|arm|arm64)$")
    # SymCrypt has no optimized backend for the other targets. Select its architecture-neutral
    # C implementation for both the library and every consumer of its layouts.
    target_compile_definitions(symcrypt PUBLIC SYMCRYPT_IGNORE_PLATFORM)
    target_compile_definitions(symcrypt_km PUBLIC SYMCRYPT_IGNORE_PLATFORM)
endif()
target_include_directories(symcrypt
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib)
target_include_directories(symcrypt_km
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib)

if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(symcrypt PRIVATE -Wno-missing-braces -Wno-unused-but-set-variable -Wno-unused-function)
    target_compile_options(symcrypt_km PRIVATE -Wno-missing-braces -Wno-unused-but-set-variable -Wno-unused-function)
elseif(MSVC)
    target_compile_options(symcrypt PRIVATE /wd4068)
    target_compile_options(symcrypt_km PRIVATE /wd4068)
endif()
