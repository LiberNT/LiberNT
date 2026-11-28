#define STANDALONE
#include <apitest.h>

extern void func_IsDeviceRegisteredWithManagement(void);

const struct test winetest_testlist[] =
{
    { "IsDeviceRegisteredWithManagement", func_IsDeviceRegisteredWithManagement },
    { 0, 0 }
};
