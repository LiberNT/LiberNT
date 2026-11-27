
include_directories(
    ${REACTOS_SOURCE_DIR}/modules/rostests/apitests/include
    ${REACTOS_SOURCE_DIR}/drivers/network/nwifi)

add_executable(nwifi_unittest RxFrame.c testlist.c)
set_module_type(nwifi_unittest win32cui)
add_importlibs(nwifi_unittest msvcrt kernel32 ntdll)
add_rostests_file(TARGET nwifi_unittest)
