#define STANDALONE
#include <apitest.h>

extern void func_SwDevice(void);

const struct test winetest_testlist[] =
{
    { "SwDevice", func_SwDevice },
    { 0, 0 }
};
