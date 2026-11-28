/* Automatically generated file; DO NOT EDIT!! */

#define STANDALONE
#include <wine/test.h>

extern void func_protocol(void);
extern void func_sock(void);
extern void func_sock_events(void);
extern void func_sock_events_ov(void);
extern void func_sock_iocp(void);
extern void func_sock_completion(void);
extern void func_sock_select(void);
extern void func_sock_io(void);

const struct test winetest_testlist[] =
{
    { "protocol", func_protocol },
    { "sock", func_sock },
    { "sock_events", func_sock_events },
    { "sock_events_ov", func_sock_events_ov },
    { "sock_iocp", func_sock_iocp },
    { "sock_completion", func_sock_completion },
    { "sock_select", func_sock_select },
    { "sock_io", func_sock_io },
    { 0, 0 }
};
