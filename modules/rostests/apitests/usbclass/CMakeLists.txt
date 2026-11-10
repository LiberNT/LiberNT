list(APPEND SOURCE
    Devices.c
    Storage.c
    Audio.c
    Network.c
    SmartCard.c
    testlist.c)

add_executable(usbclass_apitest ${SOURCE})
target_link_libraries(usbclass_apitest wine ${PSEH_LIB})
set_module_type(usbclass_apitest win32cui)
add_importlibs(usbclass_apitest setupapi winmm winscard iphlpapi ws2_32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET usbclass_apitest)
