/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     OpenGL ICD selection by the window's display adapter
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <d3dkmthk.h>
#include <GL/gl.h>
#include <string.h>
#include <wine/test.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#define MAX_ICDS 8

static const WCHAR GpuPreferencesKey[] = L"Software\\Microsoft\\DirectX\\UserGpuPreferences";

typedef struct _PREFERENCE_STATE
{
    WCHAR Path[MAX_PATH];
    WCHAR Saved[256];
    DWORD SavedSize;
    BOOL HadValue;
    BOOL Written;
} PREFERENCE_STATE;

typedef struct _ICD_SET
{
    LUID DisplayLuid;
    BOOL DisplayKnown;
    WCHAR Display[MAX_PATH];
    WCHAR Foreign[MAX_ICDS][MAX_PATH];
    ULONG ForeignCount;
} ICD_SET;

static BOOL
QueryOpenGlInfo(D3DKMT_HANDLE Adapter, D3DKMT_OPENGLINFO *Info)
{
    D3DKMT_QUERYADAPTERINFO Query;

    memset(Info, 0, sizeof(*Info));
    memset(&Query, 0, sizeof(Query));
    Query.hAdapter = Adapter;
    Query.Type = KMTQAITYPE_UMOPENGLINFO;
    Query.pPrivateDriverData = Info;
    Query.PrivateDriverDataSize = sizeof(*Info);
    return NT_SUCCESS(D3DKMTQueryAdapterInfo(&Query)) && Info->UmdOpenGlIcdFileName[0] != 0;
}

static void
CloseKmtAdapter(D3DKMT_HANDLE Adapter)
{
    D3DKMT_CLOSEADAPTER Close;

    if (Adapter == 0)
        return;
    Close.hAdapter = Adapter;
    D3DKMTCloseAdapter(&Close);
}

static void
CopyBaseName(WCHAR *Destination, const WCHAR *Path)
{
    const WCHAR *Base = wcsrchr(Path, L'\\');

    lstrcpynW(Destination, Base ? Base + 1 : Path, MAX_PATH);
}

static void
QueryDisplayIcd(HDC hdc, ICD_SET *Set)
{
    D3DKMT_OPENADAPTERFROMHDC Open;
    D3DKMT_OPENGLINFO Info;

    memset(&Open, 0, sizeof(Open));
    Open.hDc = hdc;
    Set->DisplayKnown = NT_SUCCESS(D3DKMTOpenAdapterFromHdc(&Open)) && Open.hAdapter != 0;
    if (!Set->DisplayKnown)
        return;
    Set->DisplayLuid = Open.AdapterLuid;
    if (QueryOpenGlInfo(Open.hAdapter, &Info))
        CopyBaseName(Set->Display, Info.UmdOpenGlIcdFileName);
    trace("display adapter %08lx:%08lx ICD '%ls'\n", Open.AdapterLuid.HighPart,
          Open.AdapterLuid.LowPart, Set->Display);
    CloseKmtAdapter(Open.hAdapter);
}

static void
QueryForeignIcds(ICD_SET *Set)
{
    D3DKMT_ENUMADAPTERS2 Enum;
    D3DKMT_ADAPTERINFO *Adapters;
    D3DKMT_OPENGLINFO Info;
    WCHAR Name[MAX_PATH];
    ULONG Index;

    memset(&Enum, 0, sizeof(Enum));
    if (!NT_SUCCESS(D3DKMTEnumAdapters2(&Enum)) || Enum.NumAdapters == 0)
        return;
    Adapters = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Enum.NumAdapters * sizeof(*Adapters));
    if (Adapters == NULL)
        return;
    Enum.pAdapters = Adapters;
    if (NT_SUCCESS(D3DKMTEnumAdapters2(&Enum)))
    {
        for (Index = 0; Index < Enum.NumAdapters; ++Index)
        {
            BOOL Display = Adapters[Index].AdapterLuid.LowPart == Set->DisplayLuid.LowPart &&
                           Adapters[Index].AdapterLuid.HighPart == Set->DisplayLuid.HighPart;

            if (!Display && QueryOpenGlInfo(Adapters[Index].hAdapter, &Info))
            {
                CopyBaseName(Name, Info.UmdOpenGlIcdFileName);
                trace("adapter %08lx:%08lx publishes ICD '%ls'\n", Adapters[Index].AdapterLuid.HighPart,
                      Adapters[Index].AdapterLuid.LowPart, Name);
                if (Set->ForeignCount < MAX_ICDS && lstrcmpiW(Name, Set->Display) != 0)
                    lstrcpynW(Set->Foreign[Set->ForeignCount++], Name, MAX_PATH);
            }
            CloseKmtAdapter(Adapters[Index].hAdapter);
        }
    }
    HeapFree(GetProcessHeap(), 0, Adapters);
}

static void
PreferHighPerformance(PREFERENCE_STATE *State)
{
    static const WCHAR Value[] = L"GpuPreference=2;";
    HKEY Key;
    DWORD Type = 0;

    memset(State, 0, sizeof(*State));
    if (GetModuleFileNameW(NULL, State->Path, MAX_PATH) == 0)
        return;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, GpuPreferencesKey, 0, NULL, 0, KEY_QUERY_VALUE | KEY_SET_VALUE,
                        NULL, &Key, NULL) != ERROR_SUCCESS)
        return;
    State->SavedSize = sizeof(State->Saved) - sizeof(WCHAR);
    State->HadValue = RegQueryValueExW(Key, State->Path, NULL, &Type, (BYTE *)State->Saved,
                                       &State->SavedSize) == ERROR_SUCCESS && Type == REG_SZ;
    State->Written = RegSetValueExW(Key, State->Path, 0, REG_SZ, (const BYTE *)Value,
                                    sizeof(Value)) == ERROR_SUCCESS;
    RegCloseKey(Key);
}

static void
RestorePreference(const PREFERENCE_STATE *State)
{
    HKEY Key;

    if (!State->Written ||
        RegOpenKeyExW(HKEY_CURRENT_USER, GpuPreferencesKey, 0, KEY_SET_VALUE, &Key) != ERROR_SUCCESS)
        return;
    if (State->HadValue)
        RegSetValueExW(Key, State->Path, 0, REG_SZ, (const BYTE *)State->Saved, State->SavedSize);
    else
        RegDeleteValueW(Key, State->Path);
    RegCloseKey(Key);
}

static void
CheckLoadedIcd(HDC hdc, const ICD_SET *Set)
{
    PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1 };
    const char *Renderer;
    HGLRC Context;
    ULONG Index;
    int Format;

    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    Format = ChoosePixelFormat(hdc, &pfd);
    ok(Format != 0, "ChoosePixelFormat failed: %lu\n", GetLastError());
    if (Format == 0)
        return;
    if (!SetPixelFormat(hdc, Format, &pfd))
    {
        ok(0, "SetPixelFormat(%d) failed: %lu\n", Format, GetLastError());
        return;
    }
    Context = wglCreateContext(hdc);
    ok(Context != NULL, "wglCreateContext failed: %lu\n", GetLastError());
    if (Context == NULL)
        return;
    if (wglMakeCurrent(hdc, Context))
    {
        Renderer = (const char *)glGetString(GL_RENDERER);
        trace("renderer '%s'\n", Renderer ? Renderer : "(null)");
        ok(Renderer != NULL, "GL_RENDERER is NULL\n");
        for (Index = 0; Index < Set->ForeignCount; ++Index)
            ok(GetModuleHandleW(Set->Foreign[Index]) == NULL,
               "ICD '%ls' of an adapter that does not own the window was loaded\n", Set->Foreign[Index]);
        if (Set->Display[0] != 0)
            ok(GetModuleHandleW(Set->Display) != NULL, "Display adapter ICD '%ls' was not loaded\n", Set->Display);
        wglMakeCurrent(NULL, NULL);
    }
    else
    {
        ok(0, "wglMakeCurrent failed: %lu\n", GetLastError());
    }
    wglDeleteContext(Context);
}

START_TEST(wgl_icd_adapter)
{
    PREFERENCE_STATE Preference;
    ICD_SET Set;
    HWND Window;
    HDC hdc;

    Window = CreateWindowA("static", "icd adapter", WS_POPUP | WS_VISIBLE, 0, 0, 64, 64, NULL, NULL, NULL, NULL);
    ok(Window != NULL, "CreateWindow failed: %lu\n", GetLastError());
    if (Window == NULL)
        return;
    hdc = GetDC(Window);
    ok(hdc != NULL, "GetDC failed: %lu\n", GetLastError());
    if (hdc == NULL)
    {
        DestroyWindow(Window);
        return;
    }

    memset(&Set, 0, sizeof(Set));
    QueryDisplayIcd(hdc, &Set);
    ok(Set.DisplayKnown, "D3DKMTOpenAdapterFromHdc failed for a window DC\n");
    QueryForeignIcds(&Set);
    if (Set.ForeignCount == 0)
        skip("No other adapter publishes an OpenGL ICD\n");

    PreferHighPerformance(&Preference);
    ok(Preference.Written, "Could not set the GPU preference for the test\n");
    CheckLoadedIcd(hdc, &Set);
    RestorePreference(&Preference);

    ReleaseDC(Window, hdc);
    DestroyWindow(Window);
}
