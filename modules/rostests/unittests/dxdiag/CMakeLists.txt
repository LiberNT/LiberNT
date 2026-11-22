
include_directories(
    ${REACTOS_SOURCE_DIR}/modules/rostests/apitests/include
    ${REACTOS_SOURCE_DIR}/base/applications/dxdiag
    ${REACTOS_SOURCE_DIR}/sdk/lib/udmihelp)

list(APPEND SOURCE
    D3DMark.c
    D3DStats.c
    ${REACTOS_SOURCE_DIR}/base/applications/dxdiag/d3dhud.c
    ${REACTOS_SOURCE_DIR}/base/applications/dxdiag/d3dmark.c
    ${REACTOS_SOURCE_DIR}/base/applications/dxdiag/d3dstats.c
    testlist.c)

add_executable(dxdiag_unittest ${SOURCE})
set_module_type(dxdiag_unittest win32cui)
add_importlibs(dxdiag_unittest gdi32 user32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET dxdiag_unittest)
