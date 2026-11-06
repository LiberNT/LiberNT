#define STANDALONE
#include <apitest.h>

extern void func_DriveLayout(void);
extern void func_StorDeviceNumber(void);
extern void func_VirtioDisk(void);

const struct test winetest_testlist[] =
{
    { "DriveLayout", func_DriveLayout },
    { "StorDeviceNumber", func_StorDeviceNumber },
    { "VirtioDisk", func_VirtioDisk },
    { 0, 0 }
};
