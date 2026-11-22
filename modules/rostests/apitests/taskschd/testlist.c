#define STANDALONE
#include <apitest.h>

extern void func_ExecXmlEmpty(void);
extern void func_RunTask(void);

const struct test winetest_testlist[] =
{
    { "ExecXmlEmpty", func_ExecXmlEmpty },
    { "RunTask", func_RunTask },
    { 0, 0 }
};
