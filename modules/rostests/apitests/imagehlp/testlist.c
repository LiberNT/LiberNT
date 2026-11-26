#define STANDALONE
#include <apitest.h>

extern void func_CertificateTable(void);

const struct test winetest_testlist[] =
{
    { "CertificateTable", func_CertificateTable },
    { 0, 0 }
};
