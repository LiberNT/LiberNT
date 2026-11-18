#define STANDALONE
#include <wine/test.h>

extern void func_manifest(void);

const struct test winetest_testlist[] =
{
    { "manifest", func_manifest },
    { 0, 0 }
};
