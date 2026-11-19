#define STANDALONE
#include <apitest.h>

extern void func_ClearCodec(void);
extern void func_ControllerMode(void);
extern void func_FrameMove(void);
extern void func_GfxBatch(void);
extern void func_RemoteDesktop(void);
extern void func_Zgfx(void);

const struct test winetest_testlist[] =
{
    { "ClearCodec", func_ClearCodec },
    { "ControllerMode", func_ControllerMode },
    { "FrameMove", func_FrameMove },
    { "GfxBatch", func_GfxBatch },
    { "RemoteDesktop", func_RemoteDesktop },
    { "Zgfx", func_Zgfx },
    { 0, 0 }
};
