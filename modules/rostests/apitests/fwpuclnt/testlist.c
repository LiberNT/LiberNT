#define STANDALONE
#include <apitest.h>

extern void func_ale(void);
extern void func_mpssvc(void);

const struct test winetest_testlist[] =
{
    { "ale", func_ale },
    { "mpssvc", func_mpssvc },
    { 0, 0 }
};
