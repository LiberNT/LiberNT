#define STANDALONE
#include <apitest.h>

extern void func_DWriteCache(void);
extern void func_DWriteText(void);
extern void func_GdiText(void);

const struct test winetest_testlist[] =
{
    { "DWriteCache", func_DWriteCache },
    { "DWriteText", func_DWriteText },
    { "GdiText", func_GdiText },

    { 0, 0 }
};
