
project(taskmgr11)

list(APPEND SOURCE
    theme.cpp
    render.cpp
    icons.cpp
    data.cpp
    gpu.cpp
    treelist.cpp
    menu.cpp
    controls.cpp
    dialogs.cpp
    page_processes.cpp
    page_performance.cpp
    page_apphistory.cpp
    page_startup.cpp
    page_users.cpp
    page_details.cpp
    page_services.cpp
    page_sensors.cpp
    page_settings.cpp
    main.cpp
    battery_telemetry.h
    hardware_text.h
    performance_layout.h
    wheel_scroll.h
    app.h)

file(GLOB taskmgr11_rc_deps res/*.*)
add_rc_deps(taskmgr11.rc ${taskmgr11_rc_deps})
add_executable(taskmgr11 ${SOURCE} taskmgr11.rc)
set_module_type(taskmgr11 win32gui UNICODE)
target_link_libraries(taskmgr11 uuid wdmguid dx10guid cpprt)
add_dependencies(taskmgr11 d3d_idl_headers)
add_importlibs(taskmgr11 advapi32 user32 gdi32 gdiplus shlwapi ole32 oleaut32 psapi comctl32 dwmapi uxtheme msvcrt kernel32 ntdll)
add_delay_importlibs(taskmgr11 shell32 setupapi comdlg32 wtsapi32 iphlpapi ws2_32 version)
add_pch(taskmgr11 app.h SOURCE)
add_cd_file(TARGET taskmgr11 DESTINATION reactos/system32 FOR all)
