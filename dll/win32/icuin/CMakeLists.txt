
spec2def(icuin.dll icuin.spec ADD_IMPORTLIB)

add_library(icuin MODULE ${CMAKE_CURRENT_BINARY_DIR}/icuin.def)
set_target_properties(icuin PROPERTIES LINKER_LANGUAGE C)
set_module_type(icuin win32dll ENTRYPOINT 0)
add_dependencies(icuin icu)
if(MSVC)
    # icu.spec lists the stub icu.dll; the forwarded names are those of the full ICU icu.dll
    spec2def_import_forwarded_names(icuin icu)
endif()
add_cd_file(TARGET icuin DESTINATION reactos/system32 FOR all)
