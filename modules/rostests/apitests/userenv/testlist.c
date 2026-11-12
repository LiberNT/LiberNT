
#define STANDALONE
#include <apitest.h>

extern void func_CreateEnvironmentBlock(void);
extern void func_GetProfileDirs(void);
extern void func_LoadUserProfile(void);
extern void func_ProfileCreation(void);

const struct test winetest_testlist[] =
{
    { "CreateEnvironmentBlock", func_CreateEnvironmentBlock },
    { "GetProfileDirs", func_GetProfileDirs },
    { "LoadUserProfile", func_LoadUserProfile },
    { "ProfileCreation", func_ProfileCreation },
    { 0, 0 }
};
