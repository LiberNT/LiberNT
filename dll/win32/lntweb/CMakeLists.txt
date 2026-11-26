
spec2def(lntweb.dll lntweb.spec)

list(APPEND SOURCE
    container.cpp
    control.cpp
    view.cpp
    lntwebp.h)

add_library(lntweb MODULE
    ${SOURCE}
    lntweb.rc
    ${CMAKE_CURRENT_BINARY_DIR}/lntweb.def)

set_property(TARGET lntweb PROPERTY CXX_STANDARD 17)
set_property(TARGET lntweb PROPERTY CXX_STANDARD_REQUIRED ON)
set_target_cpp_properties(lntweb WITH_EXCEPTIONS WITH_RTTI)
set_module_type(lntweb win32dll UNICODE)
target_link_libraries(lntweb litehtml lunasvg plutovg gumbo cppstl cpprt uuid)
add_importlibs(lntweb comctl32 uxtheme ole32 shlwapi user32 gdi32 advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET lntweb DESTINATION reactos/system32 FOR all)
