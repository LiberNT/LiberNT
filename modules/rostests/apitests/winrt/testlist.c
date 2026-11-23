#define STANDALONE
#include <apitest.h>

extern void func_AnalyticsInfo(void);
extern void func_AppInstallManager(void);
extern void func_CoreApplication(void);
extern void func_DispatcherQueue(void);
extern void func_NotificationData(void);
extern void func_PenDevice(void);
extern void func_ToastCallback(void);
extern void func_ToastHistory(void);
extern void func_ToastHistoryChild(void);
extern void func_ToastNotification(void);
extern void func_ValueSet(void);

const struct test winetest_testlist[] =
{
    { "AnalyticsInfo", func_AnalyticsInfo },
    { "AppInstallManager", func_AppInstallManager },
    { "CoreApplication", func_CoreApplication },
    { "DispatcherQueue", func_DispatcherQueue },
    { "NotificationData", func_NotificationData },
    { "PenDevice", func_PenDevice },
    { "ToastCallback", func_ToastCallback },
    { "ToastHistory", func_ToastHistory },
    { "ToastHistoryChild", func_ToastHistoryChild },
    { "ToastNotification", func_ToastNotification },
    { "ValueSet", func_ValueSet },

    { 0, 0 }
};
