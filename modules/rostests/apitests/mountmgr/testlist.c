#define STANDALONE
#include <apitest.h>

extern void func_NextDriveLetter(void);
extern void func_QueryDosVolumePaths(void);
extern void func_QueryPoints(void);
extern void func_RemoteDatabase(void);

const struct test winetest_testlist[] =
{
    { "NextDriveLetter", func_NextDriveLetter },
    { "QueryDosVolumePaths", func_QueryDosVolumePaths },
    { "QueryPoints", func_QueryPoints },
    { "RemoteDatabase", func_RemoteDatabase },
    { 0, 0 }
};

