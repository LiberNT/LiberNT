
#define STANDALONE
#include <apitest.h>

extern void func_CreateEnvironmentBlock(void);
extern void func_GetProfileDirs(void);
extern void func_LoadUserProfile(void);

const struct test winetest_testlist[] =
{
    { "CreateEnvironmentBlock", func_CreateEnvironmentBlock },
    { "GetProfileDirs", func_GetProfileDirs },
    { "LoadUserProfile", func_LoadUserProfile },
    { 0, 0 }
};
