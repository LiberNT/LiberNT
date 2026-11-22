#define STANDALONE
#include <apitest.h>

extern void func_protocol(void);
extern void func_UninstallDriver(void);
extern void func_UpdateDriver(void);

const struct test winetest_testlist[] =
{
    { "protocol", func_protocol },
    { "UninstallDriver", func_UninstallDriver },
    { "UpdateDriver", func_UpdateDriver },
    { 0, 0 }
};
