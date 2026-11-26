add_executable(cacls_apitest
    EditVerbs.c
    testlist.c)

target_include_directories(cacls_apitest PRIVATE ${REACTOS_SOURCE_DIR}/modules/rostests/apitests/include)
set_module_type(cacls_apitest win32cui)
add_importlibs(cacls_apitest advapi32 msvcrt kernel32)
add_rostests_file(TARGET cacls_apitest)
