#define STANDALONE
#include <apitest.h>

extern void func_layout_or_tip(void);

const struct test winetest_testlist[] =
{
    { "layout_or_tip", func_layout_or_tip },
    { 0, 0 }
};
