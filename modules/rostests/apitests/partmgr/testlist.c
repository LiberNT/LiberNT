#define STANDALONE
#include <apitest.h>

extern void func_DriveLayout(void);
extern void func_StorDeviceNumber(void);

const struct test winetest_testlist[] =
{
    { "DriveLayout", func_DriveLayout },
    { "StorDeviceNumber", func_StorDeviceNumber },
    { 0, 0 }
};
