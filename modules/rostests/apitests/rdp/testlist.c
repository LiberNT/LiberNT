#define STANDALONE
#include <apitest.h>

extern void func_ControllerMode(void);
extern void func_RemoteDesktop(void);

const struct test winetest_testlist[] =
{
    { "ControllerMode", func_ControllerMode },
    { "RemoteDesktop", func_RemoteDesktop },
    { 0, 0 }
};
