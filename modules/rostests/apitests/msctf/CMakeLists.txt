
add_definitions(-DINITGUID)

list(APPEND SOURCE
    category.cpp
    composition.cpp
    editsession.cpp
    inputmode.cpp
    langbar.cpp
    langbarmgr.cpp
    layouttip.cpp
    owner.cpp
    profiles.cpp
    property.cpp
    range.cpp
    textedit.cpp
    threadmgr.cpp
    teststore.cpp
    testtip.cpp)

list(APPEND PCH_SKIP_SOURCE
    testlist.c)

add_executable(msctf_apitest
    ${SOURCE}
    ${PCH_SKIP_SOURCE})

target_link_libraries(msctf_apitest wine uuid cpprt)
set_module_type(msctf_apitest win32cui)
add_importlibs(msctf_apitest imm32 ole32 oleaut32 advapi32 user32 msvcrt kernel32)
add_pch(msctf_apitest precomp.h "${PCH_SKIP_SOURCE}")
add_rostests_file(TARGET msctf_apitest)

spec2def(msctf_testtip.dll testtip.spec)
add_library(msctf_testtip MODULE
    testtipdll.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/msctf_testtip.def)
set_module_type(msctf_testtip win32dll)
target_link_libraries(msctf_testtip uuid cpprt)
add_importlibs(msctf_testtip imm32 user32 ole32 oleaut32 msvcrt kernel32)
add_dependencies(msctf_apitest msctf_testtip)
add_rostests_file(TARGET msctf_testtip)
