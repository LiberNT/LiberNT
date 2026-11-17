#define STANDALONE
#include <apitest.h>

extern void func_AvSetMmThreadCharacteristics(void);
extern void func_BusyWaitingThread(void);
extern void func_ManyRegistrations(void);
extern void func_SystemResponsiveness(void);
extern void func_TaskProfiles(void);

const struct test winetest_testlist[] =
{
    { "AvSetMmThreadCharacteristics", func_AvSetMmThreadCharacteristics },
    { "BusyWaitingThread", func_BusyWaitingThread },
    { "ManyRegistrations", func_ManyRegistrations },
    { "SystemResponsiveness", func_SystemResponsiveness },
    { "TaskProfiles", func_TaskProfiles },
    { 0, 0 }
};
