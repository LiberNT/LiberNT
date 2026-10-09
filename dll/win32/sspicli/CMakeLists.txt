
spec2def(sspicli.dll sspicli.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    main.c
    sspicli_main.c
    ${CMAKE_CURRENT_BINARY_DIR}/sspicli.def)

add_library(sspicli MODULE ${SOURCE})
set_module_type(sspicli win32dll ENTRYPOINT DllMain 12)
target_link_libraries(sspicli wine)
add_importlibs(sspicli secur32 msvcrt kernel32 ntdll)
add_cd_file(TARGET sspicli DESTINATION reactos/system32 FOR all)
