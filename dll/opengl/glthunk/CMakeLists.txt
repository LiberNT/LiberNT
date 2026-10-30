if(ARCH STREQUAL "amd64")
    spec2def(glthunk.dll guest/glthunk.spec)
    add_library(glthunk MODULE EXCLUDE_FROM_ALL
        guest/icd.c
        ${CMAKE_CURRENT_BINARY_DIR}/glthunk.def)
    set_module_type(glthunk win32dll ENTRYPOINT 0 UNICODE)
endif()

if(NOT (ARCH STREQUAL "riscv64" AND ENABLE_FELIX86 AND TARGET mesa_gallium_build AND
        EXISTS "${REACTOS_SOURCE_DIR}/submodules/felix86/src/felix86/v2/recompiler.cpp"))
    return()
endif()

find_program(GLTHUNK_PYTHON NAMES python3 python REQUIRED)
set(_glthunk_registry "${REACTOS_SOURCE_DIR}/submodules/mesa/src/mesa/glapi/glapi/registry/gl.xml")
set(_glthunk_icd_header "${REACTOS_SOURCE_DIR}/dll/opengl/opengl32/icd.h")
set(_glthunk_signatures "${CMAKE_CURRENT_BINARY_DIR}/glsig.c")
add_custom_command(
    OUTPUT "${_glthunk_signatures}"
    COMMAND "${GLTHUNK_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/glsig.py" "${_glthunk_registry}"
        "${_glthunk_icd_header}" "${_glthunk_signatures}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/glsig.py" "${_glthunk_registry}" "${_glthunk_icd_header}"
    VERBATIM)

add_library(glthunk_host STATIC
    host/hostrt.c
    host/glhost.c
    "${_glthunk_signatures}")
target_include_directories(glthunk_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(RV64X_COMPILER_LAUNCHER AND NOT RV64X_SYSTEM)
    set_target_properties(glthunk_host PROPERTIES C_COMPILER_LAUNCHER ${RV64X_COMPILER_LAUNCHER})
    target_compile_options(glthunk_host PRIVATE -mllvm -riscv-rv64x)
endif()
add_dependencies(glthunk_host psdk xdk)

set(_glthunk_mesa "${CMAKE_BINARY_DIR}/dll/opengl/mesa_gallium/mesa-source/cmake-build")
set(_glthunk_sdk "${CMAKE_BINARY_DIR}/dll/opengl/mesa_gallium/mesa-source/reactos-sdk-lib")
set(_glthunk_mesa_libraries)
foreach(_library mesa_assert_report gallium nir compiler mesa_util mesa_util_simd blake3 mesa_util_c11 glsl glcpp
        mesa vtn wsgdi glapi_bridge glapi galliumvl_stub xmlconfig softpipe zink vulkan_util mesa_assert_report)
    list(APPEND _glthunk_mesa_libraries "${_glthunk_mesa}/lib${_library}.a")
endforeach()

set(_glthunk_host_dll "${CMAKE_CURRENT_BINARY_DIR}/mesa_host.dll")
add_custom_command(
    OUTPUT "${_glthunk_host_dll}"
    COMMAND "${CMAKE_C_COMPILER}" --target=riscv64-w64-mingw32 -shared -nostdlib -fuse-ld=lld
        -Wl,--disable-auto-import,--disable-stdcall-fixup -Wl,-entry,DllMainCRTStartup
        -Wl,-O1 -Wl,--gc-sections -Wl,--nxcompat -Wl,--dynamicbase
        -o "${_glthunk_host_dll}"
        "${_glthunk_mesa}/CMakeFiles/mesa_gallium.dir/src/gallium/targets/wgl/wgl.c.obj"
        -Wl,--whole-archive "${_glthunk_mesa}/libwgl.a" $<TARGET_FILE:glthunk_host> -Wl,--no-whole-archive
        ${_glthunk_mesa_libraries}
        $<TARGET_FILE:libcntpr> $<TARGET_FILE:crtmath> $<TARGET_FILE:vcruntime>
        "${REACTOS_LIBCXX_ROOT}/lib/libc++.a" "${REACTOS_LIBCXX_ROOT}/lib/libc++abi.a"
        "${REACTOS_LIBCXX_ROOT}/lib/libunwind.a"
        "${_glthunk_sdk}/libmsvcrtex.a" "${_glthunk_sdk}/libuuid.a"
        $<TARGET_FILE:libamd64emu> "${_glthunk_sdk}/libntdll.a"
        "${REACTOS_LIBCXX_ROOT}/lib/windows/libclang_rt.builtins-riscv64.a"
    DEPENDS mesa_gallium_build glthunk_host libcntpr crtmath vcruntime libamd64emu
    COMMENT "Linking the native OpenGL host mesa_host.dll"
    VERBATIM)
add_custom_target(mesa_host ALL DEPENDS "${_glthunk_host_dll}")
add_cd_file(TARGET mesa_host FILE "${_glthunk_host_dll}" DESTINATION reactos/system32 FOR all)
