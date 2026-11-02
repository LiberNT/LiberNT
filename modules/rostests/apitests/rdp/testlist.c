#define STANDALONE
#include <apitest.h>

extern void func_RemoteDesktop(void);

const struct test winetest_testlist[] =
{
    { "RemoteDesktop", func_RemoteDesktop },
    { 0, 0 }
};
