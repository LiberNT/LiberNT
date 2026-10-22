#define STANDALONE
#include <apitest.h>

extern void func_AppInstallManager(void);
extern void func_NotificationData(void);
extern void func_PenDevice(void);
extern void func_ToastHistory(void);
extern void func_ToastHistoryChild(void);
extern void func_ToastNotification(void);
extern void func_ValueSet(void);

const struct test winetest_testlist[] =
{
    { "AppInstallManager", func_AppInstallManager },
    { "NotificationData", func_NotificationData },
    { "PenDevice", func_PenDevice },
    { "ToastHistory", func_ToastHistory },
    { "ToastHistoryChild", func_ToastHistoryChild },
    { "ToastNotification", func_ToastNotification },
    { "ValueSet", func_ValueSet },

    { 0, 0 }
};
