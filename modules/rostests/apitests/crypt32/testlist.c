
#define STANDALONE
#include <apitest.h>

extern void func_CertControlStore(void);
extern void func_CertEnumSystemStoreLocation(void);
extern void func_CertUpdateStore(void);
extern void func_ChainRevocation(void);
extern void func_Countersign(void);
extern void func_CryptProtectMemory(void);
extern void func_RevocationTimeout(void);

const struct test winetest_testlist[] =
{
    { "CertControlStore", func_CertControlStore },
    { "CertEnumSystemStoreLocation", func_CertEnumSystemStoreLocation },
    { "CertUpdateStore", func_CertUpdateStore },
    { "ChainRevocation", func_ChainRevocation },
    { "Countersign", func_Countersign },
    { "CryptProtectMemory", func_CryptProtectMemory },
    { "RevocationTimeout", func_RevocationTimeout },

    { 0, 0 }
};
