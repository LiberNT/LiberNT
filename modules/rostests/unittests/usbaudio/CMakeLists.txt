
include_directories(
    ${REACTOS_SOURCE_DIR}/modules/rostests/apitests/include
    ${REACTOS_SOURCE_DIR}/drivers/usb/usbaudio/uac)

add_executable(usbaudio_unittest UacStream.c testlist.c)
set_module_type(usbaudio_unittest win32cui)
add_importlibs(usbaudio_unittest msvcrt kernel32 ntdll)
add_rostests_file(TARGET usbaudio_unittest)
