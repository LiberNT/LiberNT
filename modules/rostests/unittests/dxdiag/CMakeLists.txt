
include_directories(
    ${REACTOS_SOURCE_DIR}/modules/rostests/apitests/include
    ${REACTOS_SOURCE_DIR}/base/applications/dxdiag)

list(APPEND SOURCE
    D3DMark.c
    ${REACTOS_SOURCE_DIR}/base/applications/dxdiag/d3dhud.c
    ${REACTOS_SOURCE_DIR}/base/applications/dxdiag/d3dmark.c
    testlist.c)

add_executable(dxdiag_unittest ${SOURCE})
set_module_type(dxdiag_unittest win32cui)
add_importlibs(dxdiag_unittest msvcrt kernel32 ntdll)
add_rostests_file(TARGET dxdiag_unittest)
