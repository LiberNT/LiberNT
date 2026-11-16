#define STANDALONE
#include <apitest.h>

extern void func_basicui(void);
extern void func_filesource(void);
extern void func_installend(void);
extern void func_reboot(void);

const struct test winetest_testlist[] =
{
    { "basicui", func_basicui },
    { "filesource", func_filesource },
    { "installend", func_installend },
    { "reboot", func_reboot },
    { 0, 0 }
};
