#define STANDALONE
#include <apitest.h>

extern void func_QosFlows(void);

const struct test winetest_testlist[] =
{
    { "QosFlows", func_QosFlows },
    { 0, 0 }
};
