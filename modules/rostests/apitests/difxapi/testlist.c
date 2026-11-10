#define STANDALONE
#include <apitest.h>

extern void func_DriverPackage(void);

const struct test winetest_testlist[] =
{
    { "DriverPackage", func_DriverPackage },
    { 0, 0 }
};
