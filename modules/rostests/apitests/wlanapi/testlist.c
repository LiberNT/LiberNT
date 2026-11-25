
#define STANDALONE
#include <apitest.h>

extern void func_wlanapi(void);
extern void func_WlanAdapterIdentity(void);
extern void func_WlanConnectionRecovery(void);
extern void func_WlanIhvControl(void);
extern void func_WlanProfileKeyMaterial(void);

const struct test winetest_testlist[] =
{
    { "wlanapi", func_wlanapi },
    { "WlanAdapterIdentity", func_WlanAdapterIdentity },
    { "WlanConnectionRecovery", func_WlanConnectionRecovery },
    { "WlanIhvControl", func_WlanIhvControl },
    { "WlanProfileKeyMaterial", func_WlanProfileKeyMaterial },
    { 0, 0 }
};
