
#define STANDALONE
#include <apitest.h>

extern void func_FeatureStaging(void);
extern void func_ForwardedOrdinals(void);
extern void func_IsOSDomainMember(void);

extern void func_RegistryHelpers(void);

extern void func_Win81ExportSurface(void);

const struct test winetest_testlist[] =
{
    { "FeatureStaging", func_FeatureStaging },
    { "ForwardedOrdinals", func_ForwardedOrdinals },
    { "IsOSDomainMember", func_IsOSDomainMember },
    { "RegistryHelpers", func_RegistryHelpers },
    { "Win81ExportSurface", func_Win81ExportSurface },
    { 0, 0 }
};
