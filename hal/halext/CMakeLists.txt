
add_definitions(-D_NTHAL_)

include_directories(
    ${REACTOS_SOURCE_DIR}/ntos/hal/arm64/include
    ${REACTOS_SOURCE_DIR}/hal/arch/common/include)

add_library(halextlib STATIC lib/halextlib.c)
add_dependencies(halextlib psdk bugcodes)

function(add_hal_extension _name)
    spec2def(${_name}.dll ${_name}.spec)
    add_library(${_name} MODULE ${ARGN} ${CMAKE_CURRENT_BINARY_DIR}/${_name}.def)
    set_module_type(${_name} kerneldll ENTRYPOINT 0)
    set_subsystem(${_name} native)
    add_importlibs(${_name} ntoskrnl hal)
    target_link_libraries(${_name} halextlib)
    add_dependencies(${_name} psdk bugcodes)
    add_cd_file(TARGET ${_name} DESTINATION reactos/system32 NO_CAB FOR all)
endfunction()

add_subdirectory(apple)
add_subdirectory(bcm2711)
add_subdirectory(bcm2712)
add_subdirectory(bcm2836)
