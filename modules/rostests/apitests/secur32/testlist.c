#define STANDALONE
#include <apitest.h>

extern void func_LogonRights(void);
extern void func_LogonTypes(void);
extern void func_Negotiate(void);

const struct test winetest_testlist[] =
{
    { "LogonRights", func_LogonRights },
    { "LogonTypes", func_LogonTypes },
    { "Negotiate", func_Negotiate },
    { 0, 0 }
};
