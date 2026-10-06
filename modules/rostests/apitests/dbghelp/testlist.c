
#define STANDALONE
#include <wine/test.h>

extern void func_MiniDumpSnapshot(void);
extern void func_pdb(void);

const struct test winetest_testlist[] =
{
    { "MiniDumpSnapshot", func_MiniDumpSnapshot },
    { "pdb", func_pdb },
    { 0, 0 }
};
