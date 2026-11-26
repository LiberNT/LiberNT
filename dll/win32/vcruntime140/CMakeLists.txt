
include_directories(BEFORE ${REACTOS_SOURCE_DIR}/sdk/include/wine)
get_property(INCLUDE_DIRS DIRECTORY . PROPERTY INCLUDE_DIRECTORIES)
list(REMOVE_ITEM INCLUDE_DIRS "${REACTOS_SOURCE_DIR}/sdk/include/crt")
set_property(DIRECTORY . PROPERTY INCLUDE_DIRECTORIES ${INCLUDE_DIRS})
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/ucrt)

spec2def(vcruntime140.dll vcruntime140.spec ADD_IMPORTLIB)

# TODO: init_thread.c, new.cpp, typeinfo.c and typeinfo_root.cpp belong to the import library and are not built
list(APPEND SOURCE
    misc.c
    ${CMAKE_CURRENT_BINARY_DIR}/vcruntime140_stubs.c)

add_library(vcruntime140 MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/vcruntime140.def)

set_module_type(vcruntime140 win32dll ENTRYPOINT 0)
target_link_libraries(vcruntime140 wine)
add_importlibs(vcruntime140 ucrtbase kernel32 ntdll)
add_cd_file(TARGET vcruntime140 DESTINATION reactos/system32 FOR all)
set_wine_module(vcruntime140)
