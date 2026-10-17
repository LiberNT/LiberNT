
list(APPEND SOURCE
    main.cpp
    util.cpp
    page_system.cpp
    page_sound.cpp
    page_devices.cpp
    page_network.cpp
    page_personal.cpp
    page_apps.cpp
    page_accounts.cpp
    page_time.cpp
    page_misc.cpp
    pointer.cpp
    ${REACTOS_SOURCE_DIR}/base/system/dwm/settings.c)

file(GLOB systemsettings_rc_deps res/*.* lang/*.rc)
add_rc_deps(systemsettings.rc ${systemsettings_rc_deps})
add_executable(systemsettings ${SOURCE} systemsettings.rc)
set_target_properties(systemsettings PROPERTIES OUTPUT_NAME "SystemSettings")
set_property(TARGET systemsettings PROPERTY CXX_STANDARD 17)
set_property(TARGET systemsettings PROPERTY CXX_STANDARD_REQUIRED ON)
target_include_directories(systemsettings PRIVATE ${REACTOS_SOURCE_DIR}/base/system/dwm)
set_module_type(systemsettings win32gui UNICODE)
target_link_libraries(systemsettings shellframe cppstl cpprt uuid)
add_importlibs(systemsettings advapi32 user32 gdi32 gdiplus shell32 shlwapi comctl32 comdlg32 dwmapi uxtheme ole32
               oleaut32 secur32 netapi32 powrprof wlanapi iphlpapi ws2_32 wininet bluetoothapis setupapi cfgmgr32 winspool winmm
               version msvcrt kernel32 ntdll)
add_cd_file(TARGET systemsettings DESTINATION reactos/ImmersiveControlPanel FOR all)
