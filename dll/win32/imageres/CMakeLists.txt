
add_library(imageres MODULE imageres.rc)
set_module_type(imageres win32dll UNICODE ENTRYPOINT 0)
add_cd_file(TARGET imageres DESTINATION reactos/system32 FOR all)
