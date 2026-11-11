
#define STANDALONE
#include <apitest.h>

extern void func_GuiServiceFailure(void);
extern void func_SystemCall(void);
extern void func_XStateConfig(void);
extern void func_XStateSave(void);

const struct test winetest_testlist[] =
{
    { "GuiServiceFailure",              func_GuiServiceFailure },
    { "SystemCall",                     func_SystemCall },
    { "XStateConfig",                   func_XStateConfig },
    { "XStateSave",                     func_XStateSave },

    { 0, 0 }
};
