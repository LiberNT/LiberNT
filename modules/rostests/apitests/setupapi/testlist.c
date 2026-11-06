
#define STANDALONE
#include <apitest.h>

extern void func_ClassInstallParams(void);
extern void func_devclass(void);
extern void func_DevicePowerOnDisable(void);
extern void func_DeviceRemoveVeto(void);
extern void func_InterruptAffinityPolicy(void);
extern void func_SetupDiGetActualModelsSection(void);
extern void func_InvalidDeviceInfoSet(void);
extern void func_NewDeviceInstall(void);
extern void func_SetupInstallServicesFromInfSectionEx(void);
extern void func_SetupCommitFileQueue(void);
extern void func_SetupDiInstallClassExA(void);
extern void func_SetupDiOpenDeviceInterface(void);
extern void func_SetupInstallFile(void);
extern void func_SetupInstallRegisterDlls(void);
extern void func_UsbInterfaceIds(void);

const struct test winetest_testlist[] =
{
    { "ClassInstallParams", func_ClassInstallParams },
    { "devclass", func_devclass },
    { "DevicePowerOnDisable", func_DevicePowerOnDisable },
    { "DeviceRemoveVeto", func_DeviceRemoveVeto },
    { "InterruptAffinityPolicy", func_InterruptAffinityPolicy },
    { "SetupDiGetActualModelsSection", func_SetupDiGetActualModelsSection },
    { "InvalidDeviceInfoSet", func_InvalidDeviceInfoSet },
    { "NewDeviceInstall", func_NewDeviceInstall },
    { "SetupInstallServicesFromInfSectionEx", func_SetupInstallServicesFromInfSectionEx},
    { "SetupCommitFileQueue", func_SetupCommitFileQueue },
    { "SetupDiInstallClassExA", func_SetupDiInstallClassExA},
    { "SetupDiOpenDeviceInterface", func_SetupDiOpenDeviceInterface },
    { "SetupInstallFile", func_SetupInstallFile },
    { "SetupInstallRegisterDlls", func_SetupInstallRegisterDlls },
    { "UsbInterfaceIds", func_UsbInterfaceIds },
    { 0, 0 }
};
