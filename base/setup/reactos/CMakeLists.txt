
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib
#    ${REACTOS_SOURCE_DIR}/base/setup/lib
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/drivers
    ${REACTOS_SOURCE_DIR}/sdk/lib/tzlib)

list(APPEND SOURCE
    spapisup/fileqsup.c
    spapisup/infsupp.c
    diskinfo.c
    diskpage.c
    reactos.c
    selftest.c
    wizard.c
    reactos.h)

file(GLOB reactos_rc_deps res/*.*)
list(APPEND reactos_rc_deps
    ${REACTOS_SOURCE_DIR}/base/system/userinit/res/rosbitmap.bmp
    ${REACTOS_SOURCE_DIR}/base/system/userinit/res/rosbitmap_mask.bmp)
add_rc_deps(reactos.rc ${reactos_rc_deps})
add_executable(reactos ${SOURCE} reactos.rc)

add_pch(reactos reactos.h SOURCE)
set_module_type(reactos win32gui UNICODE)
target_link_libraries(reactos uuid tzlib volresize)
add_delay_importlibs(reactos setuplib)
add_importlibs(reactos advapi32 gdi32 user32 comctl32 dwmapi shlwapi setupapi ole32 oleaut32 msvcrt kernel32 ntdll)
add_cd_file(TARGET reactos DESTINATION reactos NO_CAB FOR bootcd)
add_cd_file(TARGET reactos DESTINATION ${ARCH} FOR preinstall)
