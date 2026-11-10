
#define STANDALONE
#include <apitest.h>

extern void func_SecureFailure(void);
extern void func_WinHttpOpen(void);

const struct test winetest_testlist[] =
{
    { "SecureFailure", func_SecureFailure },
    { "WinHttpOpen", func_WinHttpOpen },

    { 0, 0 }
};
