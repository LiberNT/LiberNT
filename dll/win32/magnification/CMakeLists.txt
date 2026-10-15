
spec2def(magnification.dll magnification.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    magnification.c
    ${CMAKE_CURRENT_BINARY_DIR}/magnification_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/magnification.def)

add_library(magnification MODULE ${SOURCE})
set_module_type(magnification win32dll UNICODE ENTRYPOINT DllMain 12)
add_importlibs(magnification user32 gdi32 kernel32 ntdll)
add_cd_file(TARGET magnification DESTINATION reactos/system32 FOR all)
