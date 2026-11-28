/* Automatically generated file; DO NOT EDIT!! */

#define STANDALONE
#include <wine/test.h>

extern void func_notification(void);
extern void func_url(void);
extern void func_winhttp(void);
extern void func_winhttp_server(void);

const struct test winetest_testlist[] =
{
    { "notification", func_notification },
    { "url", func_url },
    { "winhttp", func_winhttp },
    { "winhttp_server", func_winhttp_server },
    { 0, 0 }
};
