#define STANDALONE
#include <apitest.h>

extern void func_Asn1Codecs(void);
extern void func_CatalogV2(void);
extern void func_SipCaps(void);

const struct test winetest_testlist[] =
{
    { "Asn1Codecs", func_Asn1Codecs },
    { "CatalogV2", func_CatalogV2 },
    { "SipCaps", func_SipCaps },
    { 0, 0 }
};
