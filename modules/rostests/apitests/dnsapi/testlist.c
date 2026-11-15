#define STANDALONE
#include <apitest.h>

extern void func_DnsQuery(void);
extern void func_ResolverCore(void);

const struct test winetest_testlist[] =
{
    { "DnsQuery", func_DnsQuery },
    { "ResolverCore", func_ResolverCore },
    { 0, 0 }
};
