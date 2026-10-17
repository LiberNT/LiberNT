
include_directories(${REACTOS_SOURCE_DIR}/sdk/lib/storageutils)

list(APPEND SOURCE
    main.c
    model.c
    ops.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/storageutils/mountmgrutil.c
    diskmgmt.h)

file(GLOB diskmgmt_rc_deps res/*.*)
add_rc_deps(diskmgmt.rc ${diskmgmt_rc_deps})
add_executable(diskmgmt ${SOURCE} guid.c diskmgmt.rc)
set_module_type(diskmgmt win32gui UNICODE)
target_link_libraries(diskmgmt uuid volresize)
add_importlibs(diskmgmt fmifs user32 gdi32 comctl32 shell32 shlwapi ole32 advapi32 uxtheme dwmapi msvcrt kernel32 ntdll)
add_pch(diskmgmt diskmgmt.h SOURCE)
add_cd_file(TARGET diskmgmt DESTINATION reactos/system32 FOR all)
