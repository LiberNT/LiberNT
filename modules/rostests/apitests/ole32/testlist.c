
#define STANDALONE
#include <wine/test.h>

extern void func_globaloptions(void);
extern void func_initializespy(void);
extern void func_localserver(void);
extern void func_storagestat(void);

const struct test winetest_testlist[] =
{
    { "globaloptions", func_globaloptions },
    { "initializespy", func_initializespy },
    { "localserver", func_localserver },
    { "storagestat", func_storagestat },

    { 0, 0 }
};
