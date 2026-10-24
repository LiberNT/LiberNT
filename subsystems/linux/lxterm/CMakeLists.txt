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
target_link_libraries(lxterm lxlaunch zlib)
add_importlibs(lxterm winhttp bcrypt shell32 iphlpapi advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET lxterm DESTINATION reactos/system32 FOR all)
