
#define STANDALONE
#include <apitest.h>

extern void func_CertControlStore(void);
extern void func_CertEnumSystemStoreLocation(void);
extern void func_CertUpdateStore(void);
extern void func_CryptProtectMemory(void);

const struct test winetest_testlist[] =
{
    { "CertControlStore", func_CertControlStore },
    { "CertEnumSystemStoreLocation", func_CertEnumSystemStoreLocation },
    { "CertUpdateStore", func_CertUpdateStore },
    { "CryptProtectMemory", func_CryptProtectMemory },

    { 0, 0 }
};
