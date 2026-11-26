
add_rc_deps(iexplore.rc
    ${CMAKE_CURRENT_SOURCE_DIR}/resource.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lang/en-US.rc)
add_executable(iexplore
    main.c
    browser.c
    iexplore.rc)
set_module_type(iexplore win32gui UNICODE)
add_importlibs(iexplore winhttp comctl32 comdlg32 user32 gdi32 shell32 msvcrt kernel32)
add_cd_file(TARGET iexplore DESTINATION "Program Files/Internet Explorer" FOR all)
