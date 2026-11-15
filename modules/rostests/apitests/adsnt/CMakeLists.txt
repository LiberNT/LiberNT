
list(APPEND SOURCE
    WinNTComputer.c
    WinNTGroup.c
    WinNTMoniker.c
    testlist.c)

add_executable(adsnt_apitest ${SOURCE})
target_link_libraries(adsnt_apitest uuid wine)
set_module_type(adsnt_apitest win32cui)
add_importlibs(adsnt_apitest activeds netapi32 advapi32 oleaut32 ole32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET adsnt_apitest)
