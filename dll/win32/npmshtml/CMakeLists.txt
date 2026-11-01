
spec2def(npmshtml.dll npmshtml.spec)

list(APPEND SOURCE
    main.c
    ${CMAKE_CURRENT_BINARY_DIR}/npmshtml.def)

add_library(npmshtml MODULE ${SOURCE} npmshtml.rc)
set_module_type(npmshtml win32dll)
add_importlibs(npmshtml msvcrt kernel32 ntdll)
add_cd_file(TARGET npmshtml DESTINATION reactos/system32/gecko/plugin FOR all)
set_wine_module(npmshtml)
