
include_directories(${REACTOS_SOURCE_DIR}/sdk/lib/storageutils)

list(APPEND SOURCE
    main.c
    model.c
    ops.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/storageutils/mountmgrutil.c
    diskmgmt11.h)

file(GLOB diskmgmt11_rc_deps res/*.*)
add_rc_deps(diskmgmt11.rc ${diskmgmt11_rc_deps})
add_executable(diskmgmt11 ${SOURCE} guid.c diskmgmt11.rc)
set_module_type(diskmgmt11 win32gui UNICODE)
target_link_libraries(diskmgmt11 uuid volresize)
add_importlibs(diskmgmt11 fmifs user32 gdi32 comctl32 shell32 shlwapi ole32 advapi32 uxtheme dwmapi msvcrt kernel32 ntdll)
add_pch(diskmgmt11 diskmgmt11.h SOURCE)
add_cd_file(TARGET diskmgmt11 DESTINATION reactos/system32 FOR all)
