#define STANDALONE
#include <apitest.h>

extern void func_Enum(void);
extern void func_RegSz(void);
extern void func_Descriptors(void);
extern void func_Policy(void);
extern void func_Bulk(void);
extern void func_Sizes(void);
extern void func_ShortPackets(void);
extern void func_PartialReads(void);
extern void func_RawIo(void);
extern void func_Interrupt(void);
extern void func_Control(void);
extern void func_Stall(void);
extern void func_Settings(void);
extern void func_Cancel(void);

const struct test winetest_testlist[] =
{
    { "Enum", func_Enum },
    { "RegSz", func_RegSz },
    { "Descriptors", func_Descriptors },
    { "Policy", func_Policy },
    { "Bulk", func_Bulk },
    { "Sizes", func_Sizes },
    { "ShortPackets", func_ShortPackets },
    { "PartialReads", func_PartialReads },
    { "RawIo", func_RawIo },
    { "Interrupt", func_Interrupt },
    { "Control", func_Control },
    { "Stall", func_Stall },
    { "Settings", func_Settings },
    { "Cancel", func_Cancel },
    { 0, 0 }
};
