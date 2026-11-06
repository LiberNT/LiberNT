#define STANDALONE
#include <apitest.h>

extern void func_DsCapture(void);
extern void func_DsStill(void);
extern void func_KsCaps(void);
extern void func_KsDisable(void);
extern void func_KsFilter(void);
extern void func_KsProfile(void);
extern void func_KsRestart(void);
extern void func_KsStream(void);
extern void func_KsUnplug(void);
extern void func_MfCapture(void);
extern void func_MfControls(void);
extern void func_MfUnplug(void);

const struct test winetest_testlist[] =
{
    { "DsCapture", func_DsCapture },
    { "DsStill", func_DsStill },
    { "KsCaps", func_KsCaps },
    { "KsDisable", func_KsDisable },
    { "KsFilter", func_KsFilter },
    { "KsProfile", func_KsProfile },
    { "KsRestart", func_KsRestart },
    { "KsStream", func_KsStream },
    { "KsUnplug", func_KsUnplug },
    { "MfCapture", func_MfCapture },
    { "MfControls", func_MfControls },
    { "MfUnplug", func_MfUnplug },
    { 0, 0 }
};
