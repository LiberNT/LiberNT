
include_directories(${REACTOS_SOURCE_DIR}/dll/win32/wer)

add_executable(werfault werfault.c werfault.rc)
set_module_type(werfault win32gui UNICODE)
target_link_libraries(werfault werremote)
add_importlibs(werfault wer dbghelp version psapi rpcrt4 advapi32 user32 msvcrt kernel32 ntdll)
add_cd_file(TARGET werfault DESTINATION reactos/system32 FOR all)
