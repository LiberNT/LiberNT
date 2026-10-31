
list(APPEND SOURCE
    vt.cpp
    json.cpp
    settings.cpp
    session.cpp
    view.cpp
    window.cpp
    settingsui.cpp
    main.cpp)

file(GLOB terminal_rc_deps res/*.* lang/*.rc)
add_rc_deps(terminal.rc ${terminal_rc_deps})
add_executable(terminal ${SOURCE} terminal.rc)
set_property(TARGET terminal PROPERTY CXX_STANDARD 17)
set_property(TARGET terminal PROPERTY CXX_STANDARD_REQUIRED ON)
set_module_type(terminal win32gui UNICODE)
if(MSVC)
    set_target_cpp_properties(terminal WITH_EXCEPTIONS)
endif()
target_link_libraries(terminal shellframe cppstl cpprt uuid)
add_importlibs(terminal user32 gdi32 gdiplus shell32 shlwapi comctl32 comdlg32 dwmapi uxtheme ole32 advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET terminal DESTINATION reactos/system32 FOR all)
