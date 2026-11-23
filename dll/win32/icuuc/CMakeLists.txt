
spec2def(icuuc.dll icuuc.spec ADD_IMPORTLIB)

add_library(icuuc MODULE ${CMAKE_CURRENT_BINARY_DIR}/icuuc.def)
set_target_properties(icuuc PROPERTIES LINKER_LANGUAGE C)
set_module_type(icuuc win32dll ENTRYPOINT 0)
add_dependencies(icuuc icu)
if(MSVC)
    # icu.spec lists the stub icu.dll; the forwarded names are those of the full ICU icu.dll
    spec2def_import_forwarded_names(icuuc icu)
endif()
add_cd_file(TARGET icuuc DESTINATION reactos/system32 FOR all)
