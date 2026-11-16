#define STANDALONE
#include <apitest.h>

extern void func_DeviceStatus(void);

const struct test winetest_testlist[] =
{
    { "DeviceStatus", func_DeviceStatus },
    { 0, 0 }
};
