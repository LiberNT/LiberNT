
list(APPEND SOURCE
    CertificateTable.c
    testlist.c)

add_executable(imagehlp_apitest ${SOURCE})
target_link_libraries(imagehlp_apitest wine ${PSEH_LIB})
set_module_type(imagehlp_apitest win32cui)
add_importlibs(imagehlp_apitest imagehlp msvcrt kernel32)
add_rostests_file(TARGET imagehlp_apitest)
