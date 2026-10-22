#define STANDALONE
#include <apitest.h>

extern void func_composition_present(void);
extern void func_visual_reset(void);

const struct test winetest_testlist[] =
{
    { "composition_present", func_composition_present },
    { "visual_reset", func_visual_reset },
    { 0, 0 }
};
