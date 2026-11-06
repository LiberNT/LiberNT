
list(APPEND SOURCE
    kscommon.c
    KsCaps.c
    KsDisable.c
    KsFilter.c
    KsProfile.c
    KsRestart.c
    KsStream.c
    KsUnplug.c
    DsCapture.c
    DsStill.c
    MfCapture.c
    MfControls.c
    MfUnplug.c
    testlist.c)

include_directories(${REACTOS_SOURCE_DIR}/sdk/include/dxsdk)
add_executable(lntuvc_apitest ${SOURCE})
target_link_libraries(lntuvc_apitest wine uuid strmiids mfuuid)
set_module_type(lntuvc_apitest win32cui)
add_importlibs(lntuvc_apitest setupapi cfgmgr32 ksuser mf mfplat mfreadwrite ole32 oleaut32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET lntuvc_apitest)
