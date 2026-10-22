
list(APPEND SOURCE
    AppInstallManager.c
    guid.c
    NotificationData.c
    PenDevice.c
    shortcut.c
    ToastHistory.c
    ToastNotification.c
    ValueSet.c
    testlist.c)

add_executable(winrt_apitest ${SOURCE})
target_link_libraries(winrt_apitest uuid)
set_module_type(winrt_apitest win32cui)
add_importlibs(winrt_apitest combase ole32 shell32 advapi32 user32 msvcrt kernel32)
add_dependencies(winrt_apitest psdk)
add_rostests_file(TARGET winrt_apitest)
