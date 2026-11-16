
generate_import_lib(
    libdwmwindowmanager
    api-ms-win-composition-windowmanager-l1-1-0.dll
    dwmwindowmanager.spec
    ""
    "")

add_executable(dwm main.c dwm.rc)
set_module_type(dwm win32gui UNICODE)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_link_options(dwm PRIVATE
        -Wl,-Xlink=/version:10.0
        -Wl,--major-subsystem-version,10
        -Wl,--minor-subsystem-version,0)
endif()
target_link_libraries(dwm uuid)
target_link_libraries(dwm libdwmwindowmanager)
add_importlibs(dwm dwmcore advapi32 user32 gdi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET dwm DESTINATION reactos/system32 FOR all)
