
#define STANDALONE
#include <wine/test.h>

extern void func_EnumDeviceDrivers(void);
extern void func_GetDeviceDriverFileName(void);
extern void func_GetDeviceDriverBaseName(void);
extern void func_QueryWorkingSetEx(void);

const struct test winetest_testlist[] =
{
    { "EnumDeviceDrivers",                          func_EnumDeviceDrivers },
    { "GetDeviceDriverFileName",                    func_GetDeviceDriverFileName },
    { "GetDeviceDriverBaseName",                    func_GetDeviceDriverBaseName },
    { "QueryWorkingSetEx",                          func_QueryWorkingSetEx },

    { 0, 0 }
};

