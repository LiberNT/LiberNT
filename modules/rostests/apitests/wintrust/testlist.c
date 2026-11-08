#define STANDALONE
#include <apitest.h>

extern void func_Asn1Codecs(void);
extern void func_CatalogV2(void);

const struct test winetest_testlist[] =
{
    { "Asn1Codecs", func_Asn1Codecs },
    { "CatalogV2", func_CatalogV2 },
    { 0, 0 }
};
