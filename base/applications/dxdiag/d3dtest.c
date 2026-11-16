/*
 * PROJECT:         ReactX Diagnosis Application
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            base/applications/dxdiag/d3dtest.c
 * PURPOSE:         ReactX Direct3D 7, 8 and 9 tests
 * PROGRAMMERS:     Gregor Gullwi <gbrunmar (dot) ros (at) gmail (dot) com>
 */

#include "precomp.h"

#define WIDTH   800
#define HEIGHT  600

typedef BOOL (*PD3DTEST_ROUTINE)(GUID *lpDevice, HWND hWnd, BOOL BootMode, PD3DTEST_RESULT Result);

static const struct
{
    PCSTR Name;
    PD3DTEST_ROUTINE Run;
    UINT DisplayName;
} D3DTestApis[D3DTEST_API_COUNT] =
{
    { "D3D7", D3D7Test, 0 },
    { "D3D8", D3D8Test, 0 },
    { "D3D9", D3D9Test, 0 },
    { "D3D10", D3D10Test, 0 },
    { "D3D11", D3D11Test, 0 },
    { "D3D12", D3D12Test, 0 },
    { "GL1", GL1Test, 0 },
    { "GL3", GL3Test, 0 },
    { "DDRAW", DDWindowedTest, IDS_DDTEST_WINDOWED },
    { "DDRAW_FULLSCREEN", DDFullscreenTest, IDS_DDTEST_FULLSCREEN },
};

const CHAR D3DTestShaderSource[] =
    "struct VertexIn { float4 Position : POSITION; float4 Color : COLOR; };\n"
    "struct VertexOut { float4 Position : SV_Position; float4 Color : COLOR; };\n"
    "VertexOut VSMain(VertexIn Input) { VertexOut Output; Output.Position = Input.Position; Output.Color = Input.Color; return Output; }\n"
    "float4 PSMain(VertexOut Input) : SV_Target { return Input.Color; }\n";

static BOOL D3DTestPumpMessages(VOID)
{
    MSG msg;

    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
            return FALSE;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return TRUE;
}

BOOL D3DTestContinue(PD3DTEST_RESULT Result, DWORD Start, DWORD *Elapsed)
{
    *Elapsed = GetTickCount() - Start;
    Result->Elapsed = *Elapsed;
    D3DTestStatsSample(&Result->Stats, *Elapsed, Result->Frames);
    if (*Elapsed >= D3DTEST_DURATION_MS)
        return FALSE;
    if (D3DTestPumpMessages())
        return TRUE;
    Result->Stage = "message-pump";
    Result->Status = E_ABORT;
    return FALSE;
}

BOOL D3DTestFail(PD3DTEST_RESULT Result, PCSTR Api, PCSTR Stage, HRESULT Status)
{
    CHAR Message[160];

    Result->Stage = Stage;
    Result->Status = Status;
    if (SUCCEEDED(StringCchPrintfA(Message, ARRAYSIZE(Message), "DXDIAG_%s_FAIL stage=%s hr=0x%08lx\n", Api, Stage, (ULONG)Status)))
        OutputDebugStringA(Message);
    return FALSE;
}

static VOID D3DTestMarker(D3DTEST_API Api, PCSTR Phase)
{
    CHAR Marker[64];

    if (SUCCEEDED(StringCchPrintfA(Marker, ARRAYSIZE(Marker), "DXDIAG_%sBOOT_%s\n", D3DTestApis[Api].Name, Phase)))
        OutputDebugStringA(Marker);
}

static LRESULT CALLBACK D3DTestWindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static HWND D3DTestCreateWindow(BOOL BootMode)
{
    WNDCLASSEXW Class = { sizeof(Class) };
    HINSTANCE hInstance = GetModuleHandleW(NULL);

    Class.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    Class.lpfnWndProc = D3DTestWindowProc;
    Class.hInstance = hInstance;
    Class.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    Class.lpszClassName = L"d3dtest";
    if (!RegisterClassExW(&Class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return NULL;

    return CreateWindowExW(BootMode ? WS_EX_TOPMOST : 0, Class.lpszClassName, NULL, WS_POPUP,
                           (GetSystemMetrics(SM_CXSCREEN) - WIDTH) / 2, (GetSystemMetrics(SM_CYSCREEN) - HEIGHT) / 2,
                           WIDTH, HEIGHT, NULL, NULL, hInstance, NULL);
}

static BOOL D3DRunTest(GUID *lpDevice, D3DTEST_API Api, BOOL BootMode, PD3DTEST_RESULT Result)
{
    CHAR Message[96];
    HWND hWnd;
    BOOL Passed;

    ZeroMemory(Result, sizeof(*Result));
    hWnd = D3DTestCreateWindow(BootMode);
    if (!hWnd)
        return D3DTestFail(Result, D3DTestApis[Api].Name, "create-window", HRESULT_FROM_WIN32(GetLastError()));

    ShowWindow(hWnd, SW_SHOW);
    SetForegroundWindow(hWnd);
    UpdateWindow(hWnd);
    D3DTestStatsBegin(&Result->Stats, hWnd);
    if (BootMode)
        D3DTestMarker(Api, "RENDER_BEGIN");

    Passed = D3DTestApis[Api].Run(lpDevice, hWnd, BootMode, Result);
    if (Passed && SUCCEEDED(StringCchPrintfA(Message, ARRAYSIZE(Message), "DXDIAG_%s_RESULT frames=%u ms=%lu\n",
                                             D3DTestApis[Api].Name, Result->Frames, Result->Elapsed)))
        OutputDebugStringA(Message);

    if (BootMode && Passed)
    {
        D3DTestMarker(Api, "RENDER_END");
        D3DTestMarker(Api, "CAPTURE_READY");
    }
    else if (BootMode)
    {
        WCHAR szCaption[256];
        WCHAR szError[256];

        D3DTestMarker(Api, "RENDER_FAILED");
        LoadStringW(GetModuleHandleW(NULL), IDS_MAIN_DIALOG, szCaption, ARRAYSIZE(szCaption));
        LoadStringW(GetModuleHandleW(NULL), IDS_DDTEST_ERROR, szError, ARRAYSIZE(szError));
        MessageBoxW(hWnd, szError, szCaption, MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    }

    DestroyWindow(hWnd);
    return Passed;
}

static VOID D3DTestAppendNote(HWND hPage, PCWSTR Line)
{
    HWND hNotes = GetDlgItem(hPage, IDC_TEXT_INFO);
    LRESULT Length = GetWindowTextLengthW(hNotes);

    SendMessageW(hNotes, EM_SETSEL, Length, Length);
    if (Length)
        SendMessageW(hNotes, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
    SendMessageW(hNotes, EM_REPLACESEL, FALSE, (LPARAM)Line);
}

VOID D3DTestButton(HWND hPage, GUID *lpDevice, D3DTEST_API Api, UINT ButtonId)
{
    HINSTANCE hInstance = GetModuleHandleW(NULL);
    HWND hMainDialog = GetAncestor(hPage, GA_ROOT);
    D3DTEST_RESULT Result;
    WCHAR Name[64];
    WCHAR Format[256];
    WCHAR Line[384];
    ULONG Tenths;
    HRESULT hr;

    if (D3DTestApis[Api].DisplayName)
        LoadStringW(hInstance, D3DTestApis[Api].DisplayName, Name, ARRAYSIZE(Name));
    else if (!GetDlgItemTextW(hPage, ButtonId, Name, ARRAYSIZE(Name)))
        StringCchCopyW(Name, ARRAYSIZE(Name), L"?");

    EnableWindow(hMainDialog, FALSE);
    /* TODO: run on the adapter shown by this page, not on the default adapter, for Direct3D 8 and later and OpenGL */
    if (D3DRunTest(lpDevice, Api, FALSE, &Result))
    {
        Tenths = (ULONG)((ULONGLONG)Result.Frames * 10000 / max(Result.Elapsed, 1));
        LoadStringW(hInstance, IDS_D3DTEST_BENCHMARK, Format, ARRAYSIZE(Format));
        hr = StringCchPrintfW(Line, ARRAYSIZE(Line), Format, Name, Tenths / 10, Tenths % 10, Result.Frames, Result.Elapsed);
    }
    else
    {
        LoadStringW(hInstance, IDS_D3DTEST_FAILED, Format, ARRAYSIZE(Format));
        hr = StringCchPrintfW(Line, ARRAYSIZE(Line), Format, Name, Result.Stage ? Result.Stage : "?", (ULONG)Result.Status);
    }
    EnableWindow(hMainDialog, TRUE);
    SetForegroundWindow(hMainDialog);
    if (SUCCEEDED(hr))
        D3DTestAppendNote(hPage, Line);
}

BOOL D3D78BootTests(VOID)
{
    D3DTEST_RESULT Result;
    BOOL D3D7Passed = D3DRunTest(NULL, D3DTEST_API_D3D7, TRUE, &Result);
    BOOL D3D8Passed = D3DRunTest(NULL, D3DTEST_API_D3D8, TRUE, &Result);

    return D3D7Passed && D3D8Passed;
}
