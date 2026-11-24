#define STANDALONE
#include <apitest.h>

extern void func_QosFlows(void);
extern void func_QosMarking(void);

const struct test winetest_testlist[] =
{
    { "QosFlows", func_QosFlows },
    { "QosMarking", func_QosMarking },
    { 0, 0 }
};
