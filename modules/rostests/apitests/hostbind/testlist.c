#define STANDALONE
#include <apitest.h>

extern void func_HostBinding(void);

const struct test winetest_testlist[] =
{
    { "HostBinding", func_HostBinding },
    { 0, 0 }
};
