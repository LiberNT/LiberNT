#define STANDALONE
#include <apitest.h>

extern void func_InternetShortcutSave(void);

const struct test winetest_testlist[] =
{
    { "InternetShortcutSave", func_InternetShortcutSave },
    { 0, 0 }
};
