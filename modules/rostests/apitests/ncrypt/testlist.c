#define STANDALONE
#include <apitest.h>

extern void func_KeyStorage(void);
extern void func_LegacyCiphers(void);
extern void func_KeyStorageChild(void);
extern void func_StorageProviders(void);

const struct test winetest_testlist[] =
{
    { "KeyStorage", func_KeyStorage },
    { "KeyStorageChild", func_KeyStorageChild },
    { "LegacyCiphers", func_LegacyCiphers },
    { "StorageProviders", func_StorageProviders },

    { 0, 0 }
};
