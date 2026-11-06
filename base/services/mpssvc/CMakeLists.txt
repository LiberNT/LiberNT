add_library(mpssvc_policy OBJECT policy.c filter.c)
target_include_directories(mpssvc_policy PRIVATE ${REACTOS_SOURCE_DIR}/sdk/include)
add_dependencies(mpssvc_policy psdk)

add_executable(mpssvc mpssvc.c $<TARGET_OBJECTS:mpssvc_policy>)
set_module_type(mpssvc win32cui UNICODE)
add_importlibs(mpssvc fwpuclnt advapi32 msvcrt kernel32)
add_cd_file(TARGET mpssvc DESTINATION reactos/system32 FOR all)
