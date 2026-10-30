spec2def(wow64felix.dll wow64felix.spec ADD_IMPORTLIB)

set(_felix86_adapter ${REACTOS_SOURCE_DIR}/dll/win32/amd64emu/felix86)

add_library(wow64felix MODULE
    wow64felix.c
    simulate.S
    ${_felix86_adapter}/ntsupport.c
    ${_felix86_adapter}/support.S
    ${REACTOS_SOURCE_DIR}/sdk/lib/crt/math/expm1.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/crt/math/log1p.c
    $<TARGET_OBJECTS:felix86_core>
    $<TARGET_OBJECTS:felix86_zydis>
    $<TARGET_OBJECTS:felix86_softfloat>
    $<TARGET_OBJECTS:felix86_cephes>
    ${CMAKE_CURRENT_BINARY_DIR}/wow64felix_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/wow64felix.def)

set_module_type(wow64felix win32dll ENTRYPOINT 0)
target_include_directories(wow64felix BEFORE PRIVATE ${_felix86_adapter}/compat)
target_include_directories(wow64felix PRIVATE
    ${REACTOS_SOURCE_DIR}/dll/win32/amd64emu
    ${REACTOS_SOURCE_DIR}/sdk/lib/crt/math)
target_compile_definitions(wow64felix PRIVATE
    NTDDI_VERSION=${REACTOS_TARGET_NTDDI}
    _CRT_DECLARE_NONSTDC_NAMES=1
    _CRT_NONSTDC_NO_DEPRECATE)
target_link_libraries(wow64felix
    $<TARGET_FILE:libstdc++>
    $<TARGET_FILE:libsupc++>
    crtmath
    libcntpr
    ${PSEH_LIB})
add_importlibs(wow64felix wow64 ntdll)
add_dependencies(wow64felix wow64)
add_cd_file(TARGET wow64felix DESTINATION reactos/system32 FOR all)

set_target_properties(wow64felix PROPERTIES RV64X_EXCLUDE ON)
