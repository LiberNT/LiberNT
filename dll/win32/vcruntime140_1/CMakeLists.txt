spec2def(vcruntime140_1.dll vcruntime140_1.spec)

add_library(vcruntime140_1 MODULE
    vcruntime140_1.c
    ${CMAKE_CURRENT_BINARY_DIR}/vcruntime140_1.def)

set_module_type(vcruntime140_1 win32dll)
set_target_properties(vcruntime140_1 PROPERTIES LINKER_LANGUAGE C)
target_link_libraries(vcruntime140_1 msvcrt_handler4)
add_importlibs(vcruntime140_1 ucrtbase kernel32 ntdll)
add_cd_file(TARGET vcruntime140_1 DESTINATION reactos/system32 FOR all)
