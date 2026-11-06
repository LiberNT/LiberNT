
list(APPEND SOURCE
    ClassInstallParams.c
    devclass.c
    DevicePowerOnDisable.c
    DeviceRemoveVeto.c
    InterruptAffinityPolicy.c
    InvalidDeviceInfoSet.c
    NewDeviceInstall.c
    SetupCommitFileQueue.c
    SetupDiInstallClassExA.c
    SetupDiOpenDeviceInterface.c
    SetupDiGetActualModelsSection.c
    SetupInstallFile.c
    SetupInstallRegisterDlls.c
    SetupInstallServicesFromInfSectionEx.c
    UsbInterfaceIds.c
    testlist.c)

add_executable(setupapi_apitest ${SOURCE})
set_module_type(setupapi_apitest win32cui)
add_importlibs(setupapi_apitest advapi32 cfgmgr32 setupapi user32 ole32 msvcrt kernel32)
add_rostests_file(TARGET setupapi_apitest)
