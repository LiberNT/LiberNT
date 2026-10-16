
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
    langpage.c
    libernt.c
    selftest.c
    wizard.c
    libernt.h)

file(GLOB libernt_rc_deps res/*.*)
list(APPEND libernt_rc_deps
    ${REACTOS_SOURCE_DIR}/base/system/userinit/res/rosbitmap.bmp
    ${REACTOS_SOURCE_DIR}/base/system/userinit/res/rosbitmap_mask.bmp)
add_rc_deps(libernt.rc ${libernt_rc_deps})
add_executable(libernt ${SOURCE} libernt.rc)

add_pch(libernt libernt.h SOURCE)
set_module_type(libernt win32gui UNICODE)
target_link_libraries(libernt uuid tzlib volresize)
add_delay_importlibs(libernt setuplib)
add_importlibs(libernt advapi32 gdi32 user32 comctl32 dwmapi shlwapi setupapi ole32 oleaut32 msvcrt kernel32 ntdll)
add_cd_file(TARGET libernt DESTINATION reactos NO_CAB FOR bootcd)
add_cd_file(TARGET libernt DESTINATION ${ARCH} FOR preinstall)
