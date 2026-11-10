#define STANDALONE
#include <apitest.h>

extern void func_NodeConnectionInfo(void);

const struct test winetest_testlist[] =
{
    { "NodeConnectionInfo", func_NodeConnectionInfo },
    { 0, 0 }
};
