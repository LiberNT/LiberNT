#define STANDALONE
#include <apitest.h>

extern void func_DWriteText(void);
extern void func_GdiText(void);

const struct test winetest_testlist[] =
{
    { "DWriteText", func_DWriteText },
    { "GdiText", func_GdiText },

    { 0, 0 }
};
