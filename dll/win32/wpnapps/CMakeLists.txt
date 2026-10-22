
spec2def(wpnapps.dll wpnapps.spec)

list(APPEND SOURCE
    activation.c
    data.c
    event.c
    history.c
    host.c
    identity.c
    main.c
    manager.c
    notification.c
    notifier.c
    presenter.c
    schedule.c
    scheduled.c
    store.c
    stringmap.c
    vector.c
    private.h)

add_library(wpnapps MODULE ${SOURCE} wpnapps.rc ${CMAKE_CURRENT_BINARY_DIR}/wpnapps.def)
add_idl_reg_scripts(wpnapps winrt classes.idl)
set_module_type(wpnapps win32dll)
target_link_libraries(wpnapps uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(wpnapps combase ole32 shell32 shlwapi user32 gdi32 advapi32 winmm msvcrt kernel32 ntdll)
add_dependencies(wpnapps psdk)
add_cd_file(TARGET wpnapps DESTINATION reactos/system32 FOR all)
