#define STANDALONE
#include <apitest.h>

extern void func_ExecXmlEmpty(void);

const struct test winetest_testlist[] =
{
    { "ExecXmlEmpty", func_ExecXmlEmpty },
    { 0, 0 }
};
