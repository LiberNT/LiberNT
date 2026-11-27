
#define STANDALONE
#include <apitest.h>

extern void func_ClassAddProperty(void);
extern void func_AcpiDeviceCapabilities(void);
extern void func_CfgMgr32EnumSubkeys(void);
extern void func_ClassInstallParams(void);
extern void func_devclass(void);
extern void func_DeviceClassName(void);
extern void func_DeviceDescription(void);
extern void func_DeviceExtension(void);
extern void func_DeviceInterfaceAlias(void);
extern void func_KmdfServiceSection(void);
extern void func_PackageRootDevice(void);
extern void func_DriverFeatureScore(void);
extern void func_DriverFilesNeedReboot(void);
extern void func_DriverPackageSignerScore(void);
extern void func_DevicePdoName(void);
extern void func_DeviceDisablePersist(void);
extern void func_DevicePowerOnDisable(void);
extern void func_DeviceRemoveVeto(void);
extern void func_GenericButtonStart(void);
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
extern void func_DisableableParents(void);
extern void func_HalDeviceIds(void);
extern void func_ProcessorDeviceIds(void);
extern void func_SoftwareBusRestart(void);
extern void func_SetupInstallServicesFromInfSectionEx(void);
extern void func_ReenumerateFlags(void);
extern void func_ServiceFilterNoLegacyNode(void);
extern void func_SoftwareComponent(void);
extern void func_SetupCommitFileQueue(void);
extern void func_SetupDiInstallClassExA(void);
extern void func_SetupDiInterfaceSubkeys(void);
extern void func_SetupDiOpenDeviceInterface(void);
extern void func_SetupInstallFile(void);
extern void func_SetupInstallRegisterDlls(void);
extern void func_AddRegRegistryView(void);
extern void func_SysButtonDevices(void);
extern void func_UsbHubInformation(void);
extern void func_SwDevice(void);
extern void func_UsbInterfaceIds(void);

const struct test winetest_testlist[] =
{
    { "ClassAddProperty", func_ClassAddProperty },
    { "AcpiDeviceCapabilities", func_AcpiDeviceCapabilities },
    { "CfgMgr32EnumSubkeys", func_CfgMgr32EnumSubkeys },
    { "ClassInstallParams", func_ClassInstallParams },
    { "devclass", func_devclass },
    { "DeviceClassName", func_DeviceClassName },
    { "DeviceDescription", func_DeviceDescription },
    { "DeviceExtension", func_DeviceExtension },
    { "DeviceInterfaceAlias", func_DeviceInterfaceAlias },
    { "KmdfServiceSection", func_KmdfServiceSection },
    { "PackageRootDevice", func_PackageRootDevice },
    { "DriverFeatureScore", func_DriverFeatureScore },
    { "DriverFilesNeedReboot", func_DriverFilesNeedReboot },
    { "DriverPackageSignerScore", func_DriverPackageSignerScore },
    { "DevicePdoName", func_DevicePdoName },
    { "DeviceDisablePersist", func_DeviceDisablePersist },
    { "DevicePowerOnDisable", func_DevicePowerOnDisable },
    { "DeviceRemoveVeto", func_DeviceRemoveVeto },
    { "GenericButtonStart", func_GenericButtonStart },
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
    { "DisableableParents", func_DisableableParents },
    { "HalDeviceIds", func_HalDeviceIds },
    { "ProcessorDeviceIds", func_ProcessorDeviceIds },
    { "SoftwareBusRestart", func_SoftwareBusRestart },
    { "SetupInstallServicesFromInfSectionEx", func_SetupInstallServicesFromInfSectionEx},
    { "ReenumerateFlags", func_ReenumerateFlags },
    { "ServiceFilterNoLegacyNode", func_ServiceFilterNoLegacyNode },
    { "SoftwareComponent", func_SoftwareComponent },
    { "SetupCommitFileQueue", func_SetupCommitFileQueue },
    { "SetupDiInstallClassExA", func_SetupDiInstallClassExA},
    { "SetupDiInterfaceSubkeys", func_SetupDiInterfaceSubkeys },
    { "SetupDiOpenDeviceInterface", func_SetupDiOpenDeviceInterface },
    { "SetupInstallFile", func_SetupInstallFile },
    { "SetupInstallRegisterDlls", func_SetupInstallRegisterDlls },
    { "AddRegRegistryView", func_AddRegRegistryView },
    { "SysButtonDevices", func_SysButtonDevices },
    { "UsbHubInformation", func_UsbHubInformation },
    { "SwDevice", func_SwDevice },
    { "UsbInterfaceIds", func_UsbInterfaceIds },
    { 0, 0 }
};
