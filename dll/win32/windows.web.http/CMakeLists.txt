
spec2def(windows.web.http.dll windows.web.http.spec)
add_idl_headers(windows_web_http_private_idl async_private.idl)

list(APPEND SOURCE
    async.c
    client.c
    filter.c
    headers.c
    main.c
    message.c)

add_library(windows_web_http MODULE ${SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/windows.web.http.def ${CMAKE_CURRENT_BINARY_DIR}/windows.web.http_stubs.c)
set_target_properties(windows_web_http PROPERTIES OUTPUT_NAME "Windows.Web.Http")
add_idl_reg_scripts(windows_web_http winrt classes.idl)
add_dependencies(windows_web_http windows_web_http_private_idl)
set_module_type(windows_web_http win32dll)
target_compile_definitions(windows_web_http PRIVATE __WINESRC__)
target_link_libraries(windows_web_http uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(windows_web_http winhttp combase ucrtbase msvcrt kernel32 ntdll)
add_cd_file(TARGET windows_web_http DESTINATION reactos/system32 FOR all)
