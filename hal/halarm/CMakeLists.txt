# The ARM HALs live in the kernel: hal.dll only forwards their exports to ntoskrnl.exe.
spec2def(hal.dll halarm.spec ADD_IMPORTLIB)

add_library(hal MODULE ${CMAKE_CURRENT_BINARY_DIR}/hal.def)
set_target_properties(hal PROPERTIES LINKER_LANGUAGE C)
set_module_type(hal kerneldll ENTRYPOINT 0)
add_importlibs(hal ntoskrnl)
add_dependencies(hal xdk psdk bugcodes)
add_cd_file(TARGET hal DESTINATION reactos/system32 NO_CAB FOR all)
