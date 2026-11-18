#define STANDALONE
#include <apitest.h>

extern void func_ControllerMode(void);
extern void func_RemoteDesktop(void);
extern void func_Zgfx(void);

const struct test winetest_testlist[] =
{
    { "ControllerMode", func_ControllerMode },
    { "RemoteDesktop", func_RemoteDesktop },
    { "Zgfx", func_Zgfx },
    { 0, 0 }
};
