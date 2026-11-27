
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/libernt/libs/zlib)
spec2def(lntweb.dll lntweb.spec)

list(APPEND SOURCE
    container.cpp
    control.cpp
    script.cpp
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
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(lntweb PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/libstdcxx_libernt.h>")
endif()
target_link_libraries(lntweb litehtml lunasvg plutovg quickjs woff2dec brotlidec zlib gumbo cppstl cpprt uuid)
add_importlibs(lntweb comctl32 uxtheme ole32 shlwapi user32 gdi32 advapi32 ucrtbase msvcrt kernel32 ntdll)
set_source_files_properties(lntweb.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/bootstrap.js;${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/linkedom/linkedom.js")
add_cd_file(TARGET lntweb DESTINATION reactos/system32 FOR all)
