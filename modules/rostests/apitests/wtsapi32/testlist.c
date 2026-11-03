#define STANDALONE
#include <wine/test.h>

extern void func_SessionInformation(void);

const struct test winetest_testlist[] =
{
    { "SessionInformation", func_SessionInformation },
    { 0, 0 }
};
