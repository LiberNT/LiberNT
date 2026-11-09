#define STANDALONE
#include <apitest.h>

extern void func_installend(void);
extern void func_reboot(void);

const struct test winetest_testlist[] =
{
    { "installend", func_installend },
    { "reboot", func_reboot },
    { 0, 0 }
};
