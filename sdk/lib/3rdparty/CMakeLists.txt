
add_subdirectory(adns)
add_subdirectory(cardlib)
add_subdirectory(fatfs)
add_subdirectory(faudio)
add_subdirectory(freetype)
add_subdirectory(jxr)
add_subdirectory(libgsm)
add_subdirectory(lcms2)
if(ENABLE_LNTWEB AND NOT MSVC)
    add_subdirectory(brotli)
    add_subdirectory(litehtml)
    add_subdirectory(lunasvg)
    add_subdirectory(plutovg)
    add_subdirectory(quickjs)
    add_subdirectory(woff2)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        foreach(_target litehtml lunasvg woff2dec)
            target_compile_options(${_target} PRIVATE "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/libstdcxx_libernt.h")
        endforeach()
        target_compile_options(litehtml PRIVATE -fno-trapping-math)
    endif()
endif()
add_subdirectory(ldap)
add_subdirectory(libmpg123)
add_subdirectory(libsamplerate)
add_subdirectory(libwine)
add_subdirectory(libwin-iconv)
add_subdirectory(libxml2)
add_subdirectory(lwext4)
if(ENABLE_RDPSERVER AND RDPSERVER_FROM_SOURCE)
    add_subdirectory(openssl)
    add_subdirectory(freerdp)
    set(_rdpserver_payload "${CMAKE_BINARY_DIR}/libernt-rdpserver-${ARCH}-${CMAKE_C_COMPILER_ID}.zip")
    set(_rdpserver_payload_libraries)
    foreach(_library libcrypto libssl winpr freerdp freerdp-server)
        list(APPEND _rdpserver_payload_libraries "${_library}=$<TARGET_FILE:${_library}>")
    endforeach()
    string(JOIN "|" _rdpserver_payload_libraries ${_rdpserver_payload_libraries})
    add_custom_command(
        OUTPUT "${_rdpserver_payload}"
        COMMAND "${CMAKE_COMMAND}"
            "-DOUTPUT=${_rdpserver_payload}"
            "-DARCH=${ARCH}"
            "-DCOMPILER=${CMAKE_C_COMPILER_ID}"
            "-DAR=${CMAKE_AR}"
            "-DLIBRARIES=${_rdpserver_payload_libraries}"
            "-DINCLUDES=$<JOIN:$<TARGET_PROPERTY:libcrypto,INTERFACE_INCLUDE_DIRECTORIES>,|>|$<JOIN:$<TARGET_PROPERTY:winpr,INTERFACE_INCLUDE_DIRECTORIES>,|>"
            "-DLICENSES=FreeRDP-LICENSE=${REACTOS_SOURCE_DIR}/submodules/freerdp/LICENSE|OpenSSL-LICENSE=${REACTOS_SOURCE_DIR}/submodules/openssl/LICENSE.txt"
            -P "${REACTOS_SOURCE_DIR}/submodules/rdpserver-payload-pack.cmake"
        DEPENDS libcrypto libssl winpr freerdp freerdp-server "${REACTOS_SOURCE_DIR}/submodules/rdpserver-payload-pack.cmake"
        VERBATIM)
    add_custom_target(rdpserver_payload DEPENDS "${_rdpserver_payload}")
endif()
if(MSVC)
    add_subdirectory(stlport)
endif()
add_subdirectory(strmbase)
add_subdirectory(symcrypt)
add_subdirectory(vkd3d)
add_subdirectory(wimlib)
add_subdirectory(zlib)
add_subdirectory(zstd)
