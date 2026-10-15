
add_library(kernel32_muitest MODULE muitest_ln.rc)
set_module_type(kernel32_muitest module UNICODE ENTRYPOINT 0)
add_rostests_file(TARGET kernel32_muitest)

add_library(kernel32_muitest_mui MODULE muitest_mui.rc)
set_module_type(kernel32_muitest_mui module UNICODE ENTRYPOINT 0)
add_rostests_file(TARGET kernel32_muitest_mui SUBDIR en-US RENAME kernel32_muitest.dll.mui)
