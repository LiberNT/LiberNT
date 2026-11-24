list(APPEND SOURCE
    BringUp8168H.c
    EtherCrc32.c
    FwFormat.c
    MediaConnect.c
    testlist.c)

add_executable(rtl8168_apitest ${SOURCE})
target_include_directories(rtl8168_apitest PRIVATE
    ${REACTOS_SOURCE_DIR}/drivers/network/dd/rtl8168)
target_link_libraries(rtl8168_apitest wine)
set_module_type(rtl8168_apitest win32cui)
add_importlibs(rtl8168_apitest iphlpapi ws2_32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET rtl8168_apitest)
