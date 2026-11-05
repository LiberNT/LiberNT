
#define STANDALONE
#include <apitest.h>

extern void func_AcpiDeviceCapabilities(void);
extern void func_ClassInstallParams(void);
extern void func_devclass(void);
extern void func_DeviceClassName(void);
extern void func_DeviceDescription(void);
extern void func_DeviceExtension(void);
extern void func_DriverFeatureScore(void);
extern void func_DriverFilesNeedReboot(void);
extern void func_DriverPackageSignerScore(void);
extern void func_DevicePdoName(void);
extern void func_DevicePowerOnDisable(void);
extern void func_DeviceRemoveVeto(void);
extern void func_HostBridgeStart(void);
extern void func_InboxClasses(void);
extern void func_InterruptAffinityPolicy(void);
extern void func_SetupDiGetActualModelsSection(void);
extern void func_InvalidDeviceInfoSet(void);
extern void func_LegacyDeviceParent(void);
extern void func_NetInterfaceNames(void);
extern void func_NewDeviceInstall(void);
extern void func_ParentIdPrefix(void);
extern void func_PciBridgeWindows(void);
extern void func_PciDebuggerDevice(void);
extern void func_PciDeviceIds(void);
extern void func_ProcessorDeviceIds(void);
extern void func_SetupInstallServicesFromInfSectionEx(void);
extern void func_SoftwareComponent(void);
extern void func_SetupCommitFileQueue(void);
extern void func_SetupDiInstallClassExA(void);
extern void func_SetupDiOpenDeviceInterface(void);
extern void func_SetupInstallFile(void);
extern void func_SetupInstallRegisterDlls(void);
extern void func_SysButtonDevices(void);
extern void func_UsbHubInformation(void);
extern void func_SwDevice(void);
extern void func_UsbInterfaceIds(void);

const struct test winetest_testlist[] =
{
    { "AcpiDeviceCapabilities", func_AcpiDeviceCapabilities },
    { "ClassInstallParams", func_ClassInstallParams },
    { "devclass", func_devclass },
    { "DeviceClassName", func_DeviceClassName },
    { "DeviceDescription", func_DeviceDescription },
    { "DeviceExtension", func_DeviceExtension },
    { "DriverFeatureScore", func_DriverFeatureScore },
    { "DriverFilesNeedReboot", func_DriverFilesNeedReboot },
    { "DriverPackageSignerScore", func_DriverPackageSignerScore },
    { "DevicePdoName", func_DevicePdoName },
    { "DevicePowerOnDisable", func_DevicePowerOnDisable },
    { "DeviceRemoveVeto", func_DeviceRemoveVeto },
    { "HostBridgeStart", func_HostBridgeStart },
    { "InboxClasses", func_InboxClasses },
    { "InterruptAffinityPolicy", func_InterruptAffinityPolicy },
    { "SetupDiGetActualModelsSection", func_SetupDiGetActualModelsSection },
    { "InvalidDeviceInfoSet", func_InvalidDeviceInfoSet },
    { "LegacyDeviceParent", func_LegacyDeviceParent },
    { "NetInterfaceNames", func_NetInterfaceNames },
    { "NewDeviceInstall", func_NewDeviceInstall },
    { "ParentIdPrefix", func_ParentIdPrefix },
    { "PciBridgeWindows", func_PciBridgeWindows },
    { "PciDebuggerDevice", func_PciDebuggerDevice },
    { "PciDeviceIds", func_PciDeviceIds },
    { "ProcessorDeviceIds", func_ProcessorDeviceIds },
    { "SetupInstallServicesFromInfSectionEx", func_SetupInstallServicesFromInfSectionEx},
    { "SoftwareComponent", func_SoftwareComponent },
    { "SetupCommitFileQueue", func_SetupCommitFileQueue },
    { "SetupDiInstallClassExA", func_SetupDiInstallClassExA},
    { "SetupDiOpenDeviceInterface", func_SetupDiOpenDeviceInterface },
    { "SetupInstallFile", func_SetupInstallFile },
    { "SetupInstallRegisterDlls", func_SetupInstallRegisterDlls },
    { "SysButtonDevices", func_SysButtonDevices },
    { "UsbHubInformation", func_UsbHubInformation },
    { "SwDevice", func_SwDevice },
    { "UsbInterfaceIds", func_UsbInterfaceIds },
    { 0, 0 }
};
