#define STANDALONE
#include <apitest.h>

extern void func_Asn1Codecs(void);
extern void func_CatalogV2(void);
extern void func_SignedCatalog(void);
extern void func_SipCaps(void);
extern void func_TrustHelpers(void);
extern void func_TrustSteps(void);

const struct test winetest_testlist[] =
{
    { "Asn1Codecs", func_Asn1Codecs },
    { "CatalogV2", func_CatalogV2 },
    { "SignedCatalog", func_SignedCatalog },
    { "SipCaps", func_SipCaps },
    { "TrustHelpers", func_TrustHelpers },
    { "TrustSteps", func_TrustSteps },
    { 0, 0 }
};
