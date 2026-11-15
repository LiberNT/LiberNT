#define STANDALONE
#include <apitest.h>

extern void func_WinNTGroup(void);

const struct test winetest_testlist[] =
{
    { "WinNTGroup", func_WinNTGroup },
    { 0, 0 }
};
