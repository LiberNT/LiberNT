
spec2def(dbgcore.dll dbgcore.spec)

list(APPEND SOURCE
    dbgcore.rc
    ${CMAKE_CURRENT_BINARY_DIR}/dbgcore.def)

add_library(dbgcore MODULE ${SOURCE})
set_module_type(dbgcore win32dll ENTRYPOINT 0 )
add_importlibs(dbgcore dbghelp)
add_dependencies(dbgcore psdk)
add_cd_file(TARGET dbgcore DESTINATION reactos/system32 FOR all)
