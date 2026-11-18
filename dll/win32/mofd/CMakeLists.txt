
spec2def(mofd.dll mofd.spec)

list(APPEND SOURCE
    main.c
    mofparse.c
    mofstore.c
    ${CMAKE_CURRENT_BINARY_DIR}/mofd.def)

add_library(mofd MODULE ${SOURCE} mofd.rc)
set_module_type(mofd win32dll UNICODE)
target_link_libraries(mofd wine_dll_canunload wine_dll_register uuid)
add_importlibs(mofd oleaut32 ole32 msvcrt kernel32 ntdll)
add_cd_file(TARGET mofd DESTINATION reactos/system32/wbem FOR all)
