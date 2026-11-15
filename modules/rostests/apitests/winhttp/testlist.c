
#define STANDALONE
#include <apitest.h>

extern void func_GzipChunkedReuse(void);
extern void func_LongHeader(void);
extern void func_SecureFailure(void);
extern void func_WinHttpOpen(void);

const struct test winetest_testlist[] =
{
    { "GzipChunkedReuse", func_GzipChunkedReuse },
    { "LongHeader", func_LongHeader },
    { "SecureFailure", func_SecureFailure },
    { "WinHttpOpen", func_WinHttpOpen },

    { 0, 0 }
};
