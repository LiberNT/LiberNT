
include_directories(${REACTOS_SOURCE_DIR}/win32ss)

spec2def(win32u.dll win32u.spec ADD_IMPORTLIB)
if(WOW64_I386_RUNTIME AND ARCH STREQUAL "i386")
    set_source_files_properties(sys-stubs.S PROPERTIES COMPILE_DEFINITIONS WOW64_I386_RUNTIME)
endif()
add_asm_files(win32u_asm sys-stubs.S)

add_library(win32u MODULE
    main.c
    ${win32u_asm}
    ${CMAKE_CURRENT_BINARY_DIR}/win32u.def)

set_module_type(win32u module)
add_dependencies(win32u psdk)
add_cd_file(TARGET win32u DESTINATION reactos/system32 FOR all)

if (STACK_PROTECTOR)
    target_compile_options(win32u PRIVATE -fno-stack-protector)
endif()
