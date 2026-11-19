#define STANDALONE
#include <apitest.h>

extern void func_StartSession(void);

const struct test winetest_testlist[] =
{
    { "StartSession", func_StartSession },
    { 0, 0 }
};
