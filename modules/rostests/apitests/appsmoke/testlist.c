#define STANDALONE
#include <apitest.h>

extern void func_alert_wait(void);
extern void func_child_start(void);
extern void func_d3d12_device(void);
extern void func_d3dkmt(void);
extern void func_display_mode(void);
extern void func_dll_directory(void);
extern void func_opencl(void);
extern void func_opengl(void);
extern void func_opengl_mode_change(void);
extern void func_saved_bitmap(void);
extern void func_os_version(void);
extern void func_sockets(void);
extern void func_suite(void);
extern void func_swap_chain(void);
extern void func_window_dc(void);
extern void func_yield_fairness(void);

const struct test winetest_testlist[] =
{
    { "alert_wait", func_alert_wait },
    { "child_start", func_child_start },
    { "d3d12_device", func_d3d12_device },
    { "d3dkmt", func_d3dkmt },
    { "display_mode", func_display_mode },
    { "dll_directory", func_dll_directory },
    { "opencl", func_opencl },
    { "opengl", func_opengl },
    { "opengl_mode_change", func_opengl_mode_change },
    { "os_version", func_os_version },
    { "saved_bitmap", func_saved_bitmap },
    { "sockets", func_sockets },
    { "suite", func_suite },
    { "swap_chain", func_swap_chain },
    { "window_dc", func_window_dc },
    { "yield_fairness", func_yield_fairness },
    { 0, 0 }
};
