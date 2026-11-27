
#define STANDALONE
#include <apitest.h>

extern void func_wlanapi(void);
extern void func_WlanAdapterIdentity(void);
extern void func_WlanConnectionRecovery(void);
extern void func_WlanStalledHandshake(void);
extern void func_WlanIfTableChurn(void);
extern void func_WlanAutoSwitch(void);
extern void func_WlanIhvControl(void);
extern void func_WlanProfileKeyMaterial(void);

const struct test winetest_testlist[] =
{
    { "wlanapi", func_wlanapi },
    { "WlanAdapterIdentity", func_WlanAdapterIdentity },
    { "WlanConnectionRecovery", func_WlanConnectionRecovery },
    { "WlanStalledHandshake", func_WlanStalledHandshake },
    { "WlanIfTableChurn", func_WlanIfTableChurn },
    { "WlanAutoSwitch", func_WlanAutoSwitch },
    { "WlanIhvControl", func_WlanIhvControl },
    { "WlanProfileKeyMaterial", func_WlanProfileKeyMaterial },
    { 0, 0 }
};
