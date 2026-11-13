#define STANDALONE
#include <apitest.h>

extern void func_DriveLayout(void);
extern void func_PhysicalDriveRaw(void);
extern void func_StorAdapter(void);
extern void func_StorDeviceNumber(void);
extern void func_SuperFloppy(void);
extern void func_SystemPartition(void);
extern void func_VirtioDisk(void);

const struct test winetest_testlist[] =
{
    { "DriveLayout", func_DriveLayout },
    { "PhysicalDriveRaw", func_PhysicalDriveRaw },
    { "StorAdapter", func_StorAdapter },
    { "StorDeviceNumber", func_StorDeviceNumber },
    { "SuperFloppy", func_SuperFloppy },
    { "SystemPartition", func_SystemPartition },
    { "VirtioDisk", func_VirtioDisk },
    { 0, 0 }
};
