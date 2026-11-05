
list(APPEND SOURCE
    AcpiDeviceCapabilities.c
    ClassInstallParams.c
    devclass.c
    DeviceClassName.c
    DeviceDescription.c
    DeviceExtension.c
    DriverFeatureScore.c
    DriverPackageSignerScore.c
    DevicePdoName.c
    DevicePowerOnDisable.c
    DeviceRemoveVeto.c
    HostBridgeStart.c
    InboxClasses.c
    InterruptAffinityPolicy.c
    InvalidDeviceInfoSet.c
    LegacyDeviceParent.c
    NetInterfaceNames.c
    NewDeviceInstall.c
    ParentIdPrefix.c
    PciBridgeWindows.c
    PciDebuggerDevice.c
    PciDeviceIds.c
    ProcessorDeviceIds.c
    SetupCommitFileQueue.c
    SetupDiInstallClassExA.c
    SetupDiOpenDeviceInterface.c
    SetupDiGetActualModelsSection.c
    SetupInstallFile.c
    SetupInstallRegisterDlls.c
    SetupInstallServicesFromInfSectionEx.c
    SoftwareComponent.c
    SysButtonDevices.c
    UsbHubInformation.c
    SwDevice.c
    UsbInterfaceIds.c
    testlist.c)

add_executable(setupapi_apitest ${SOURCE})
set_module_type(setupapi_apitest win32cui)
add_importlibs(setupapi_apitest advapi32 cfgmgr32 newdev setupapi user32 ole32 msvcrt kernel32)
add_rostests_file(TARGET setupapi_apitest)
