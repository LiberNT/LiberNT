#define STANDALONE
#include <apitest.h>

extern void func_KeyStorage(void);
extern void func_KeyStorageChild(void);

const struct test winetest_testlist[] =
{
    { "KeyStorage", func_KeyStorage },
    { "KeyStorageChild", func_KeyStorageChild },

    { 0, 0 }
};
