
#define STANDALONE
#include <apitest.h>

extern void func_wlanapi(void);
extern void func_WlanConnectionRecovery(void);

const struct test winetest_testlist[] =
{
    { "wlanapi", func_wlanapi },
    { "WlanConnectionRecovery", func_WlanConnectionRecovery },
    { 0, 0 }
};
