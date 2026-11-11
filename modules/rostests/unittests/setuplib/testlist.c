#define STANDALONE
#include <apitest.h>

extern void func_IsValidInstallDirectory(void);
extern void func_PartitionListRegions(void);
extern void func_PrepareUpgradeFileCopy(void);

const struct test winetest_testlist[] =
{
    { "IsValidInstallDirectory", func_IsValidInstallDirectory },
    { "PartitionListRegions", func_PartitionListRegions },
    { "PrepareUpgradeFileCopy", func_PrepareUpgradeFileCopy },
    { 0, 0 }
};
