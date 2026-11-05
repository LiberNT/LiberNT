#define STANDALONE
#include <wine/test.h>

extern void func_SessionInformation(void);
extern void func_TermSrvReady(void);

const struct test winetest_testlist[] =
{
    { "SessionInformation", func_SessionInformation },
    { "TermSrvReady", func_TermSrvReady },
    { 0, 0 }
};
