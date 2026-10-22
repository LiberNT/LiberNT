
#define STANDALONE
#include <wine/test.h>

extern void func_globaloptions(void);
extern void func_initializespy(void);

const struct test winetest_testlist[] =
{
    { "globaloptions", func_globaloptions },
    { "initializespy", func_initializespy },

    { 0, 0 }
};
