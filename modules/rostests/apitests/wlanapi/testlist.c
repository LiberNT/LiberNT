
#define STANDALONE
#include <apitest.h>

extern void func_wlanapi(void);
extern void func_WlanAdapterIdentity(void);
extern void func_WlanConnectionRecovery(void);
extern void func_WlanIhvControl(void);

const struct test winetest_testlist[] =
{
    { "wlanapi", func_wlanapi },
    { "WlanAdapterIdentity", func_WlanAdapterIdentity },
    { "WlanConnectionRecovery", func_WlanConnectionRecovery },
    { "WlanIhvControl", func_WlanIhvControl },
    { 0, 0 }
};
