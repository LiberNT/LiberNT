add_executable(tray_lifetime tray_lifetime.cpp power_lifetime.cpp tray_lifetime.rc)
target_include_directories(tray_lifetime PRIVATE ${REACTOS_SOURCE_DIR}/base/shell/explorer)
target_compile_definitions(tray_lifetime PRIVATE _ATL_NO_EXCEPTIONS)
if(DBG)
    target_compile_definitions(tray_lifetime PRIVATE _DEBUG=1)
endif()
target_link_libraries(tray_lifetime shellframe uuid wine cpprt atl_classes)
if(MSVC)
    target_compile_options(tray_lifetime PRIVATE /Gy /Gw)
else()
    target_compile_options(tray_lifetime PRIVATE -ffunction-sections -fdata-sections)
endif()
set_module_type(tray_lifetime win32cui UNICODE)
add_importlibs(tray_lifetime setupapi advapi32 gdi32 gdiplus user32 comctl32 ole32 oleaut32 shell32
    explorerframe shlwapi shdocvw version uxtheme winmm iphlpapi powrprof msvcrt kernel32 ntdll)
add_rc_deps(tray_lifetime.rc
    ${REACTOS_SOURCE_DIR}/dll/shellext/stobject/resources/tray/battery_1.ico
    ${REACTOS_SOURCE_DIR}/dll/shellext/stobject/resources/tray/battery_4.ico)
