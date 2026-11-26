#define STANDALONE
#include <apitest.h>

extern void func_SettingsChoiceCommit(void);

const struct test winetest_testlist[] =
{
    { "SettingsChoiceCommit", func_SettingsChoiceCommit },
    { 0, 0 }
};
