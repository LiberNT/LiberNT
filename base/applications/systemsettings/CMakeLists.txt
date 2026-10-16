
file(GLOB systemsettings_rc_deps res/*.* lang/*.rc)
add_rc_deps(systemsettings.rc ${systemsettings_rc_deps})
add_executable(systemsettings main.cpp systemsettings.rc)
set_target_properties(systemsettings PROPERTIES OUTPUT_NAME "SystemSettings")
set_module_type(systemsettings win32gui UNICODE)
target_link_libraries(systemsettings shellframe uuid cpprt)
add_importlibs(systemsettings advapi32 user32 gdi32 gdiplus shell32 shlwapi comctl32 dwmapi msvcrt kernel32 ntdll)
add_cd_file(TARGET systemsettings DESTINATION reactos/ImmersiveControlPanel FOR all)
