#ifndef PRECOMP_H__
#define PRECOMP_H__

#define DIRECTINPUT_VERSION 0x0800
#define DIRECTSOUND_VERSION 0x0800
#define D3D_OVERLOADS

#include <stdarg.h>

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <wingdi.h>
#include <winnls.h>
#include <winuser.h>
#include <setupapi.h>
#include <ddraw.h>
#include <initguid.h>
#include <devguid.h>
#include <strsafe.h>
#include <udmihelp.h>

#include "resource.h"
#include "d3dmark.h"

#define D3DTEST_DURATION_MS 4000

typedef enum
{
    D3DTEST_API_D3D7,
    D3DTEST_API_D3D8,
    D3DTEST_API_D3D9,
    D3DTEST_API_D3D10,
    D3DTEST_API_D3D11,
    D3DTEST_API_D3D12,
    D3DTEST_API_GL1,
    D3DTEST_API_GL3,
    D3DTEST_API_DDRAW,
    D3DTEST_API_DDRAW_FULLSCREEN,
    D3DTEST_API_COUNT
} D3DTEST_API;

#define D3DTEST_STATS_MAX_ADAPTERS 4
#define D3DTEST_STATS_MAX_NODES 16
#define D3DTEST_STATS_LINES 4
#define D3DTEST_STATS_LINE_LENGTH 24

typedef struct
{
    LUID Luid;
    UINT NodeCount;
    ULONGLONG NodeRunning[D3DTEST_STATS_MAX_NODES];
} D3DTEST_GPU;

typedef struct
{
    DWORD SampleTime;
    UINT SampleFrames;
    BOOL HaveFps;
    ULONG FpsTenths;
    LONG CpuLoad;
    LONG GpuLoad;
    ULONGLONG CpuIdle;
    ULONGLONG CpuTotal;
    UINT GpuCount;
    D3DTEST_GPU Gpus[D3DTEST_STATS_MAX_ADAPTERS];
} D3DTEST_STATS;

typedef struct
{
    UINT Frames;
    DWORD Elapsed;
    PCSTR Stage;
    HRESULT Status;
    D3DTEST_STATS Stats;
} D3DTEST_RESULT, *PD3DTEST_RESULT;

typedef struct
{
    HWND hDisplayWnd;
    GUID guid;
} DXDIAG_DISPLAY, *PDXDIAG_DISPLAY;

typedef struct
{
    HWND hMainDialog;
    HWND hTabCtrl;
    ULONG NumDisplayAdapter;
    PDXDIAG_DISPLAY * DisplayAdapters;
    ULONG NumSoundAdapter;
    HWND * hSoundWnd;
    HWND hDialogs[5];
}DXDIAG_CONTEXT, *PDXDIAG_CONTEXT;

/* globals */
extern HINSTANCE hInst;

/* theming hack */
BOOL EnableDialogTheme(HWND hwnd);

/* dialog wnd proc */
INT_PTR CALLBACK SystemPageWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK DisplayPageWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK SoundPageWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK MusicPageWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK InputPageWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK NetworkPageWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK HelpPageWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

/* DirectDraw tests */
BOOL DDWindowedTest(GUID *lpDevice, HWND hWnd, BOOL BootMode, PD3DTEST_RESULT Result);
BOOL DDFullscreenTest(GUID *lpDevice, HWND hWnd, BOOL BootMode, PD3DTEST_RESULT Result);

/* Direct3D and OpenGL tests */
extern const CHAR D3DTestShaderSource[];
VOID D3DTestButton(HWND hPage, GUID *lpDevice, D3DTEST_API Api, UINT ButtonId);
BOOL D3D78BootTests(VOID);
BOOL D3DTestContinue(PD3DTEST_RESULT Result, DWORD Start, DWORD *Elapsed);
BOOL D3DTestFail(PD3DTEST_RESULT Result, PCSTR Api, PCSTR Stage, HRESULT Status);
VOID D3DTestStatsBegin(D3DTEST_STATS *Stats);
VOID D3DTestStatsSample(D3DTEST_STATS *Stats, DWORD Elapsed, UINT Frames);
UINT D3DTestStatsText(const D3DTEST_RESULT *Result, CHAR Text[D3DTEST_STATS_LINES][D3DTEST_STATS_LINE_LENGTH]);
UINT D3DTestBuildScene(PD3DTEST_VERTEX Vertices, DWORD Elapsed, UINT Width, UINT Height, PD3DTEST_RESULT Result);
VOID D3DTestDrawStats(HDC hdc, const D3DTEST_RESULT *Result, UINT Height);
BOOL D3D7Test(GUID *lpDevice, HWND hWnd, BOOL BootMode, PD3DTEST_RESULT Result);
BOOL D3D8Test(GUID *lpDevice, HWND hWnd, BOOL BootMode, PD3DTEST_RESULT Result);
BOOL D3D9Test(GUID *lpDevice, HWND hWnd, BOOL BootMode, PD3DTEST_RESULT Result);
BOOL D3D10Test(GUID *lpDevice, HWND hWnd, BOOL BootMode, PD3DTEST_RESULT Result);
BOOL D3D11Test(GUID *lpDevice, HWND hWnd, BOOL BootMode, PD3DTEST_RESULT Result);
BOOL D3D12Test(GUID *lpDevice, HWND hWnd, BOOL BootMode, PD3DTEST_RESULT Result);
BOOL GL1Test(GUID *lpDevice, HWND hWnd, BOOL BootMode, PD3DTEST_RESULT Result);
BOOL GL3Test(GUID *lpDevice, HWND hWnd, BOOL BootMode, PD3DTEST_RESULT Result);

/* DirectSound initialization */
void InitializeDirectSoundPage(PDXDIAG_CONTEXT pContext);

/* display adapter initialization */
void InitializeDisplayAdapters(PDXDIAG_CONTEXT pContext);

/* general functions */
BOOL GetFileVersion(LPCWSTR szAppName, WCHAR * szVer, DWORD szVerSize);
BOOL GetFileModifyTime(LPCWSTR pFullPath, WCHAR * szTime, int szTimeSize);
BOOL GetCatFileFromDriverPath(LPWSTR szFileName, LPWSTR szCatFileName);
BOOL GetRegValue(HKEY hBaseKey, LPWSTR SubKey, LPWSTR ValueName, DWORD Type, LPWSTR Result, DWORD Size);
VOID InsertTabCtrlItem(HWND hDlgCtrl, INT Position, LPWSTR uId);
VOID EnumerateDrivers(PVOID Context, HDEVINFO hList, PSP_DEVINFO_DATA pInfoData);

#endif /* PRECOMP_H__ */
