#define STANDALONE
#include <apitest.h>

extern void func_NewSessionAdapter(void);
extern void func_StartSession(void);

const struct test winetest_testlist[] =
{
    { "NewSessionAdapter", func_NewSessionAdapter },
    { "StartSession", func_StartSession },
    { 0, 0 }
};
