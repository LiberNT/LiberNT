#define STANDALONE
#include <apitest.h>

extern void func_Devices(void);
extern void func_Storage(void);
extern void func_Audio(void);
extern void func_Network(void);
extern void func_SmartCard(void);

const struct test winetest_testlist[] =
{
    { "Devices", func_Devices },
    { "Storage", func_Storage },
    { "Audio", func_Audio },
    { "Network", func_Network },
    { "SmartCard", func_SmartCard },
    { 0, 0 }
};
