include_directories(
    ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/lzma
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib)

add_executable(lxterm
    lxterm.c
    install.c
    xz.c
    gz.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/lzma/LzmaDec.c
    lxterm.rc)
target_compile_definitions(lxterm PRIVATE _7ZIP_ST)
set_module_type(lxterm win32cui UNICODE)
target_link_libraries(lxterm lxlaunch zlib uuid)
add_importlibs(lxterm winhttp bcrypt shell32 user32 iphlpapi advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET lxterm DESTINATION reactos/system32 FOR all)

add_link("Linux Terminal" "shell:system\\lxterm.exe" ICON_INDEX 0)
add_custom_target(lxterm_links DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/Linux Terminal.lnk")
add_cd_file(FILE "${CMAKE_CURRENT_BINARY_DIR}/Linux Terminal.lnk" TARGET lxterm_links
            DESTINATION "ProgramData/Microsoft/Windows/Start Menu/Programs" FOR livecd preinstall)
