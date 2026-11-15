#define STANDALONE
#include <apitest.h>

extern void func_layout_or_tip(void);
extern void func_tip_profile(void);

const struct test winetest_testlist[] =
{
    { "layout_or_tip", func_layout_or_tip },
    { "tip_profile", func_tip_profile },
    { 0, 0 }
};
