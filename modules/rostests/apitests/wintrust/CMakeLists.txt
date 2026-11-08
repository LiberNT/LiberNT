
list(APPEND SOURCE
    Asn1Codecs.c
    CatalogV2.c
    SipCaps.c
    testlist.c)

add_executable(wintrust_apitest ${SOURCE})
target_link_libraries(wintrust_apitest wine)
set_module_type(wintrust_apitest win32cui)
add_importlibs(wintrust_apitest wintrust crypt32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET wintrust_apitest)
