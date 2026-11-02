#define STANDALONE
#include <apitest.h>

extern void func_RemoteDesktopPage(void);
extern void func_SettingsChoiceCommit(void);

const struct test winetest_testlist[] =
{
    { "RemoteDesktopPage", func_RemoteDesktopPage },
    { "SettingsChoiceCommit", func_SettingsChoiceCommit },
    { 0, 0 }
};
