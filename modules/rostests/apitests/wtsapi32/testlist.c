#define STANDALONE
#include <wine/test.h>

extern void func_EnumerateProcessesA(void);
extern void func_SessionInformation(void);
extern void func_TermSrvReady(void);

const struct test winetest_testlist[] =
{
    { "EnumerateProcessesA", func_EnumerateProcessesA },
    { "SessionInformation", func_SessionInformation },
    { "TermSrvReady", func_TermSrvReady },
    { 0, 0 }
};
