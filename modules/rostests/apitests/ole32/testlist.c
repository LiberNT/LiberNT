
#define STANDALONE
#include <wine/test.h>

extern void func_globaloptions(void);
extern void func_initializespy(void);
extern void func_localserver(void);
extern void func_storagestat(void);
extern void func_uninitquit(void);

const struct test winetest_testlist[] =
{
    { "globaloptions", func_globaloptions },
    { "initializespy", func_initializespy },
    { "localserver", func_localserver },
    { "storagestat", func_storagestat },
    { "uninitquit", func_uninitquit },

    { 0, 0 }
};
