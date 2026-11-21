#ifndef __MULTIMON_H
#define __MULTIMON_H

#ifdef __cplusplus
extern "C" {
#endif

HMONITOR WINAPI MonitorFromRect(LPCRECT,DWORD);
HMONITOR WINAPI MonitorFromWindow(HWND,DWORD);
HMONITOR WINAPI MonitorFromPoint(POINT,DWORD);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
BOOL WINAPI xEnumDisplayDevices(PVOID, DWORD, PDISPLAY_DEVICE, DWORD);
BOOL WINAPI xEnumDisplayMonitors(HDC, LPCRECT, MONITORENUMPROC, LPARAM);
BOOL WINAPI xGetMonitorInfo(HMONITOR, LPMONITORINFO);
int WINAPI xGetSystemMetrics(int);
HMONITOR WINAPI xMonitorFromPoint(POINT, DWORD);
HMONITOR WINAPI xMonitorFromRect(LPCRECT, DWORD);
HMONITOR WINAPI xMonitorFromWindow(HWND, DWORD);

#ifdef __cplusplus
}
#endif

#endif /* __MULTIMON_H */
