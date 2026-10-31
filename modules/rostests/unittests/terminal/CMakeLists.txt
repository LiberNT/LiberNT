
include_directories(
    ${REACTOS_SOURCE_DIR}/modules/rostests/apitests/include
    ${REACTOS_SOURCE_DIR}/base/applications/terminal)

list(APPEND SOURCE
    VtScreen.cpp
    ${REACTOS_SOURCE_DIR}/base/applications/terminal/vt.cpp
    testlist.c)

add_executable(terminal_unittest ${SOURCE})
set_property(TARGET terminal_unittest PROPERTY CXX_STANDARD 17)
set_property(TARGET terminal_unittest PROPERTY CXX_STANDARD_REQUIRED ON)
if(MSVC)
    set_target_cpp_properties(terminal_unittest WITH_EXCEPTIONS)
endif()
target_link_libraries(terminal_unittest cppstl cpprt)
set_module_type(terminal_unittest win32cui)
add_importlibs(terminal_unittest msvcrt kernel32 ntdll)
add_rostests_file(TARGET terminal_unittest)
