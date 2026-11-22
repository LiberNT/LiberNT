#define STANDALONE
#include <apitest.h>

extern void func_PickFolders(void);

const struct test winetest_testlist[] =
{
    { "PickFolders", func_PickFolders },
    { 0, 0 }
};
