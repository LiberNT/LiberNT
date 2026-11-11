
spec2def(wer.dll wer.spec ADD_IMPORTLIB)

add_library(werremote werremote.c)
add_dependencies(werremote psdk)

list(APPEND SOURCE
    wer.c
    ${CMAKE_CURRENT_BINARY_DIR}/wer.def)

add_library(wer MODULE ${SOURCE})
set_module_type(wer win32dll)
target_link_libraries(wer werremote)
add_importlibs(wer advapi32 rpcrt4 msvcrt kernel32 ntdll)
add_cd_file(TARGET wer DESTINATION reactos/system32 FOR all)
