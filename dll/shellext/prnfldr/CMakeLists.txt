
spec2def(prnfldr.dll prnfldr.spec)

list(APPEND SOURCE
    prnfldr.cpp
    CPrinterFolder.cpp
    precomp.h)

add_library(prnfldr MODULE
    ${SOURCE}
    prnfldr.rc
    ${CMAKE_CURRENT_BINARY_DIR}/prnfldr.def)

set_module_type(prnfldr win32dll UNICODE)
target_link_libraries(prnfldr uuid cpprt atl_classes)
add_importlibs(prnfldr winspool shlwapi shell32 ole32 oleaut32 user32 advapi32 msvcrt kernel32 ntdll)
add_pch(prnfldr precomp.h SOURCE)
add_cd_file(TARGET prnfldr DESTINATION reactos/system32 FOR all)
