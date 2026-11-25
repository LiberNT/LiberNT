
#define STANDALONE
#include <apitest.h>

extern void func_CertControlStore(void);
extern void func_CertEnumSystemStoreLocation(void);
extern void func_CertUpdateStore(void);
extern void func_ChainNotBefore(void);
extern void func_ChainRevocation(void);
extern void func_Countersign(void);
extern void func_CryptProtectMemory(void);
extern void func_CryptStringToBinary(void);
extern void func_FindOIDInfo(void);
extern void func_GeneralizedTime(void);
extern void func_RevocationTimeout(void);
extern void func_RootProgram(void);

const struct test winetest_testlist[] =
{
    { "CertControlStore", func_CertControlStore },
    { "CertEnumSystemStoreLocation", func_CertEnumSystemStoreLocation },
    { "CertUpdateStore", func_CertUpdateStore },
    { "ChainNotBefore", func_ChainNotBefore },
    { "ChainRevocation", func_ChainRevocation },
    { "Countersign", func_Countersign },
    { "CryptProtectMemory", func_CryptProtectMemory },
    { "CryptStringToBinary", func_CryptStringToBinary },
    { "FindOIDInfo", func_FindOIDInfo },
    { "GeneralizedTime", func_GeneralizedTime },
    { "RevocationTimeout", func_RevocationTimeout },
    { "RootProgram", func_RootProgram },

    { 0, 0 }
};
