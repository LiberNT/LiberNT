add_rc_deps(camera.rc
    ${CMAKE_CURRENT_SOURCE_DIR}/lang/en-US.rc
    ${CMAKE_CURRENT_SOURCE_DIR}/lang/ru-RU.rc)
add_executable(camera camera.c camera.rc)
set_module_type(camera win32gui UNICODE)
target_link_libraries(camera mfuuid uuid wine)
add_importlibs(camera mf mfplat mfreadwrite windowscodecs ole32 user32 gdi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET camera DESTINATION reactos/system32 FOR all)
