
#define STANDALONE
#include <apitest.h>

extern void func_HeadCache(void);
extern void func_InternetOpen(void);

const struct test winetest_testlist[] =
{
    { "HeadCache", func_HeadCache },
    { "InternetOpen", func_InternetOpen },

    { 0, 0 }
};
