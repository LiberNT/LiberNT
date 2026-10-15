#define STANDALONE
#include <apitest.h>

extern void func_WinStationConnectAndLockDesktop(void);

const struct test winetest_testlist[] =
{
    { "WinStationConnectAndLockDesktop", func_WinStationConnectAndLockDesktop },
    { 0, 0 }
};
