
#define STANDALONE
#include <apitest.h>

extern void func_devclass(void);
extern void func_InvalidDeviceInfoSet(void);
extern void func_SetupInstallServicesFromInfSectionEx(void);
extern void func_SetupDiInstallClassExA(void);
extern void func_SetupInstallFile(void);

const struct test winetest_testlist[] =
{
    { "devclass", func_devclass },
    { "InvalidDeviceInfoSet", func_InvalidDeviceInfoSet },
    { "SetupInstallServicesFromInfSectionEx", func_SetupInstallServicesFromInfSectionEx},
    { "SetupDiInstallClassExA", func_SetupDiInstallClassExA},
    { "SetupInstallFile", func_SetupInstallFile },
    { 0, 0 }
};
