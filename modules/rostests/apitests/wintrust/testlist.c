#define STANDALONE
#include <apitest.h>

extern void func_Asn1Codecs(void);
extern void func_CatalogV2(void);
extern void func_FindCerts(void);
extern void func_Internals(void);
extern void func_IssuerConfidence(void);
extern void func_Registration(void);
extern void func_SignedCatalog(void);
extern void func_SipCaps(void);
extern void func_TrustHelpers(void);
extern void func_TrustSteps(void);

const struct test winetest_testlist[] =
{
    { "Asn1Codecs", func_Asn1Codecs },
    { "CatalogV2", func_CatalogV2 },
    { "FindCerts", func_FindCerts },
    { "Internals", func_Internals },
    { "IssuerConfidence", func_IssuerConfidence },
    { "Registration", func_Registration },
    { "SignedCatalog", func_SignedCatalog },
    { "SipCaps", func_SipCaps },
    { "TrustHelpers", func_TrustHelpers },
    { "TrustSteps", func_TrustSteps },
    { 0, 0 }
};
