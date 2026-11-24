#pragma once

/* Monitor object */
typedef struct _MONITOR
{
    HEAD head;
    struct _MONITOR* pMonitorNext;
    union
    {
        DWORD   dwMONFlags;
        struct
        {
            DWORD   IsVisible: 1;
            DWORD   IsPalette: 1;
            DWORD   IsPrimary: 1; /* Whether this is the primary monitor */
        };
    };
    RECT    rcMonitor;
    RECT    rcWork;
    HRGN    hrgnMonitor;
    SHORT   cFullScreen;
    SHORT   cWndStack;
    HDEV    hDev;
} MONITOR, *PMONITOR;

NTSTATUS NTAPI UserAttachMonitor(IN HDEV hDev);
NTSTATUS NTAPI UserDetachMonitor(HDEV hDev);
NTSTATUS NTAPI UserUpdateMonitorSize(IN HDEV hDev);
NTSTATUS NTAPI UserSetMonitorDevice(IN HDEV hDevOld, IN HDEV hDevNew);
PMONITOR NTAPI UserGetMonitorObject(IN HMONITOR);
PMONITOR NTAPI UserGetPrimaryMonitor(VOID);
PMONITOR NTAPI UserGetMonitorFromHDev(HDEV hDev);
PMONITOR NTAPI UserMonitorFromRect(PRECTL,DWORD);
PMONITOR FASTCALL UserMonitorFromPoint(POINT,DWORD);
UINT NTAPI UserGetMonitorRects(PRECTL,UINT);

/* EOF */
