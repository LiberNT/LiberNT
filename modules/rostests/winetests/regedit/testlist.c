/* Automatically generated file; DO NOT EDIT!! */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define STANDALONE
#include "wine/test.h"

extern void func_regedit(void);
extern void func_regedit_invalid(void);
extern void func_regedit_comments(void);
extern void func_regedit_keys(void);
extern void func_regedit_export(void);

const struct test winetest_testlist[] =
{
    { "regedit", func_regedit },
    { "regedit_invalid", func_regedit_invalid },
    { "regedit_comments", func_regedit_comments },
    { "regedit_keys", func_regedit_keys },
    { "regedit_export", func_regedit_export },
    { 0, 0 }
};
