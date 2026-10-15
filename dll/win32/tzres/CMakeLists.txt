
add_library(tzres MODULE tzres.rc)
set_module_type(tzres module UNICODE ENTRYPOINT 0)
add_dependencies(tzres xdk)
add_cd_file(TARGET tzres DESTINATION reactos/system32 FOR all)
