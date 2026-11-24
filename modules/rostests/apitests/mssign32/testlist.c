#define STANDALONE
#include <apitest.h>

extern void func_SignerSign(void);

const struct test winetest_testlist[] =
{
    { "SignerSign", func_SignerSign },
    { 0, 0 }
};
