/* Automatically generated file; DO NOT EDIT!! */

#define STANDALONE
#include "wine/test.h"

extern void func_batch(void);
extern void func_batch_bat(void);
extern void func_batch_cmdline(void);
extern void func_directory(void);

const struct test winetest_testlist[] =
{
    { "batch", func_batch },
    { "batch_bat", func_batch_bat },
    { "batch_cmdline", func_batch_cmdline },
    { "directory", func_directory },
    { 0, 0 }
};
