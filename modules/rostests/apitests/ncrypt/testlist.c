#define STANDALONE
#include <apitest.h>

extern void func_KeyStorage(void);
extern void func_LegacyCiphers(void);
extern void func_KeyStorageChild(void);

const struct test winetest_testlist[] =
{
    { "KeyStorage", func_KeyStorage },
    { "KeyStorageChild", func_KeyStorageChild },
    { "LegacyCiphers", func_LegacyCiphers },

    { 0, 0 }
};
