#define STANDALONE
#include <apitest.h>

extern void func_WinNTGroup(void);
extern void func_WinNTMoniker(void);

const struct test winetest_testlist[] =
{
    { "WinNTGroup", func_WinNTGroup },
    { "WinNTMoniker", func_WinNTMoniker },
    { 0, 0 }
};
