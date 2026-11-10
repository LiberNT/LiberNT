#define STANDALONE
#include <apitest.h>

extern void func_Layer(void);
extern void func_Path(void);

const struct test winetest_testlist[] =
{
    { "Layer", func_Layer },
    { "Path", func_Path },

    { 0, 0 }
};
