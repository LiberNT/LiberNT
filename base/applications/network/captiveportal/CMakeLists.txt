
add_rc_deps(captiveportal.rc
    ${CMAKE_CURRENT_SOURCE_DIR}/resource.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lang/en-US.rc)
add_executable(captiveportal
    main.c
    html.c
    captiveportal.rc)
set_module_type(captiveportal win32gui UNICODE)
add_importlibs(captiveportal winhttp shlwapi comctl32 advapi32 user32 gdi32 shell32 msvcrt kernel32)
add_cd_file(TARGET captiveportal DESTINATION reactos/system32 FOR all)
