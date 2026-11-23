#define STANDALONE
#include <apitest.h>

extern void func_forward(void);
extern void func_ustring(void);

const struct test winetest_testlist[] =
{
    { "forward", func_forward },
    { "ustring", func_ustring },
    { 0, 0 }
};
