#define STANDALONE
#include <apitest.h>

extern void func_exe(void);
extern void func_inno(void);
extern void func_nullsoft(void);
extern void func_portable(void);
extern void func_wix(void);

const struct test winetest_testlist[] =
{
    { "exe", func_exe },
    { "inno", func_inno },
    { "nullsoft", func_nullsoft },
    { "portable", func_portable },
    { "wix", func_wix },
    { 0, 0 }
};
