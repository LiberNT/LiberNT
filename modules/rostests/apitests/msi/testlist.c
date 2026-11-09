#define STANDALONE
#include <apitest.h>

extern void func_reboot(void);

const struct test winetest_testlist[] =
{
    { "reboot", func_reboot },
    { 0, 0 }
};
