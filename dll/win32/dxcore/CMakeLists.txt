
spec2def(dxcore.dll dxcore.spec ADD_IMPORTLIB)

add_library(dxcore MODULE
    dxcore.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/dxcore.def)

set_module_type(dxcore win32dll UNICODE)
target_link_libraries(dxcore cpprt uuid)
add_importlibs(dxcore gdi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET dxcore DESTINATION reactos/system32 FOR all)
