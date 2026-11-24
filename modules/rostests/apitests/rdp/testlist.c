#define STANDALONE
#include <apitest.h>

extern void func_Avc420(void);
extern void func_ClearCodec(void);
extern void func_ControllerMode(void);
extern void func_ControllerSecret(void);
extern void func_Experience(void);
extern void func_FrameMove(void);
extern void func_GfxBatch(void);
extern void func_GfxCaps(void);
extern void func_OpenH264(void);
extern void func_OtherUser(void);
extern void func_PasswordMemory(void);
extern void func_PipeSquat(void);
extern void func_PreAuthSession(void);
extern void func_RemoteDesktop(void);
extern void func_RemoteLogonType(void);
extern void func_ServiceCrash(void);
extern void func_ServiceStop(void);
extern void func_TileKey(void);
extern void func_VideoDetect(void);
extern void func_Zgfx(void);

const struct test winetest_testlist[] =
{
    { "Avc420", func_Avc420 },
    { "ClearCodec", func_ClearCodec },
    { "ControllerMode", func_ControllerMode },
    { "ControllerSecret", func_ControllerSecret },
    { "Experience", func_Experience },
    { "FrameMove", func_FrameMove },
    { "GfxBatch", func_GfxBatch },
    { "GfxCaps", func_GfxCaps },
    { "OpenH264", func_OpenH264 },
    { "OtherUser", func_OtherUser },
    { "PasswordMemory", func_PasswordMemory },
    { "PipeSquat", func_PipeSquat },
    { "PreAuthSession", func_PreAuthSession },
    { "RemoteDesktop", func_RemoteDesktop },
    { "RemoteLogonType", func_RemoteLogonType },
    { "ServiceCrash", func_ServiceCrash },
    { "ServiceStop", func_ServiceStop },
    { "TileKey", func_TileKey },
    { "VideoDetect", func_VideoDetect },
    { "Zgfx", func_Zgfx },
    { 0, 0 }
};
