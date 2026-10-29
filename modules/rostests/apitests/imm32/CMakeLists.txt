
list(APPEND SOURCE
    clientimc.c
    cuas.cpp
    ../msctf/testtip.cpp
    himc.c
    imcc.c
    ImmEnumInputContext.c
    ImmGetImeInfoEx.c
    ImmIsUIMessage.c
    JapanImeConvTestA.c
    JapanImeConvTestW.c
    KLID.c
    testlist.c
    resource.rc)

set_source_files_properties(cuas.cpp ../msctf/testtip.cpp PROPERTIES COMPILE_DEFINITIONS INITGUID)
add_executable(imm32_apitest ${SOURCE})
target_link_libraries(imm32_apitest wine uuid cpprt ${PSEH_LIB})
set_module_type(imm32_apitest win32cui)
add_importlibs(imm32_apitest advapi32 imm32 ole32 oleaut32 msvcrt user32 kernel32 ntdll)
add_dependencies(imm32_apitest msctf_testtip)
add_rostests_file(TARGET imm32_apitest)
