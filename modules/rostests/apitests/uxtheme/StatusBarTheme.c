/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the ExplorerStatusBar theme class and a status bar drawn with it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <stdio.h>

static const struct
{
    int Property;
    COLORREF Color;
} EdgeColors[] =
{
    { TMT_EDGELIGHTCOLOR, RGB(180, 180, 180) },
    { TMT_EDGEHIGHLIGHTCOLOR, RGB(244, 247, 252) },
    { TMT_EDGESHADOWCOLOR, RGB(180, 180, 180) },
    { TMT_EDGEDKSHADOWCOLOR, RGB(100, 100, 100) },
    { TMT_EDGEFILLCOLOR, RGB(220, 220, 220) },
};

static void TestClass(LPCWSTR Class, COLORREF Fill, COLORREF Text)
{
    HTHEME Theme;
    COLORREF Color;
    HRESULT Hr;
    ULONG Index;

    SetLastError(0xdeadbeef);
    Theme = OpenThemeData(NULL, Class);
    ok(Theme != NULL, "%ls: OpenThemeData failed with %lu\n", Class, GetLastError());
    if (!Theme)
        return;

    Color = 0xdeadbeef;
    Hr = GetThemeColor(Theme, 0, 0, TMT_FILLCOLOR, &Color);
    ok(Hr == S_OK, "%ls: FillColor hr 0x%08lx\n", Class, Hr);
    ok(Color == Fill, "%ls: FillColor 0x%06lx, expected 0x%06lx\n", Class, Color, Fill);

    Color = 0xdeadbeef;
    Hr = GetThemeColor(Theme, 0, 0, TMT_TEXTCOLOR, &Color);
    ok(Hr == S_OK, "%ls: TextColor hr 0x%08lx\n", Class, Hr);
    ok(Color == Text, "%ls: TextColor 0x%06lx, expected 0x%06lx\n", Class, Color, Text);

    for (Index = 0; Index < ARRAYSIZE(EdgeColors); Index++)
    {
        Color = 0xdeadbeef;
        Hr = GetThemeColor(Theme, 0, 0, EdgeColors[Index].Property, &Color);
        ok(Hr == S_OK, "%ls: property %d hr 0x%08lx\n", Class, EdgeColors[Index].Property, Hr);
        ok(Color == EdgeColors[Index].Color, "%ls: property %d color 0x%06lx, expected 0x%06lx\n",
           Class, EdgeColors[Index].Property, Color, EdgeColors[Index].Color);
    }

    ok(!IsThemePartDefined(Theme, SP_PANE, 0), "%ls: the pane part is defined\n", Class);
    CloseThemeData(Theme);
}

static COLORREF PixelColor(DWORD Pixel)
{
    return RGB((Pixel >> 16) & 0xFF, (Pixel >> 8) & 0xFF, Pixel & 0xFF);
}

static void TestRender(HWND Parent, LPCWSTR SubApp, COLORREF Background)
{
    BITMAPINFO Info = { 0 };
    COLORREF Colors[32];
    ULONG Counts[32] = { 0 };
    ULONG Found = 0, Index, Best = 0;
    HBITMAP Bitmap;
    DWORD *Bits;
    HDC Screen, Memory;
    HWND Bar;
    RECT Rect;
    HRESULT Hr;
    int X, Y, Width, Height;
    COLORREF Fill;

    Bar = CreateWindowExW(0, STATUSCLASSNAMEW, L"WWWWWWWWWWWW", WS_CHILD | WS_VISIBLE,
                          0, 0, 0, 0, Parent, NULL, NULL, NULL);
    ok(Bar != NULL, "%ls: status bar not created, error %lu\n", SubApp ? SubApp : L"-", GetLastError());
    if (!Bar)
        return;

    Hr = SetWindowTheme(Bar, SubApp, L"ExplorerStatusBar");
    ok(Hr == S_OK, "%ls: SetWindowTheme hr 0x%08lx\n", SubApp ? SubApp : L"-", Hr);
    SendMessageW(Bar, WM_SIZE, 0, 0);
    ok(GetWindowTheme(Bar) != NULL, "%ls: the status bar has no theme\n", SubApp ? SubApp : L"-");

    GetClientRect(Bar, &Rect);
    Width = Rect.right;
    Height = Rect.bottom;
    ok(Width > 60 && Height > 8, "%ls: status bar is %dx%d\n", SubApp ? SubApp : L"-", Width, Height);
    if (Width <= 60 || Height <= 8)
    {
        DestroyWindow(Bar);
        return;
    }

    Info.bmiHeader.biSize = sizeof(Info.bmiHeader);
    Info.bmiHeader.biWidth = Width;
    Info.bmiHeader.biHeight = -Height;
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = 32;
    Screen = GetDC(NULL);
    Memory = CreateCompatibleDC(Screen);
    Bitmap = CreateDIBSection(Screen, &Info, DIB_RGB_COLORS, (void **)&Bits, NULL, 0);
    ok(Bitmap != NULL, "CreateDIBSection failed\n");
    if (Bitmap)
    {
        SelectObject(Memory, Bitmap);
        memset(Bits, 0x5A, Width * Height * sizeof(DWORD));
        SendMessageW(Bar, WM_PRINTCLIENT, (WPARAM)Memory, PRF_CLIENT | PRF_ERASEBKGND);
        GdiFlush();

        Fill = PixelColor(Bits[(Height / 2) * Width + (Width - 40)]);
        ok(Fill == Background, "%ls: background 0x%06lx, expected 0x%06lx\n",
           SubApp ? SubApp : L"-", Fill, Background);

        for (Y = 2; Y < Height - 2; Y++)
        {
            for (X = 2; X < 200 && X < Width; X++)
            {
                COLORREF Color = PixelColor(Bits[Y * Width + X]);

                if (Color == Fill)
                    continue;
                for (Index = 0; Index < Found; Index++)
                {
                    if (Colors[Index] == Color)
                        break;
                }
                if (Index == Found && Found < ARRAYSIZE(Colors))
                    Colors[Found++] = Color;
                if (Index < Found)
                    Counts[Index]++;
            }
        }
        for (Index = 1; Index < Found; Index++)
        {
            if (Counts[Index] > Counts[Best])
                Best = Index;
        }
        ok(Found > 0, "%ls: no text was drawn\n", SubApp ? SubApp : L"-");
        if (Found > 0)
        {
            ok(Colors[Best] == GetSysColor(COLOR_BTNTEXT), "%ls: text 0x%06lx, expected 0x%06lx\n",
               SubApp ? SubApp : L"-", Colors[Best], GetSysColor(COLOR_BTNTEXT));
        }
        DeleteObject(Bitmap);
    }
    DeleteDC(Memory);
    ReleaseDC(NULL, Screen);
    DestroyWindow(Bar);
}

static HANDLE ActivateCommonControls6(ULONG_PTR *Cookie)
{
    static const char Manifest[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
        "<dependency><dependentAssembly><assemblyIdentity type=\"win32\" name=\"Microsoft.Windows.Common-Controls\" "
        "version=\"6.0.0.0\" processorArchitecture=\"*\" publicKeyToken=\"6595b64144ccf1df\" language=\"*\"/>"
        "</dependentAssembly></dependency></assembly>";
    ACTCTXW Context = { sizeof(Context) };
    WCHAR Path[MAX_PATH];
    HANDLE File, ActCtx;
    DWORD Written;

    GetTempPathW(ARRAYSIZE(Path), Path);
    lstrcatW(Path, L"StatusBarTheme.manifest");
    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile failed with %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    WriteFile(File, Manifest, sizeof(Manifest) - 1, &Written, NULL);
    CloseHandle(File);

    Context.lpSource = Path;
    ActCtx = CreateActCtxW(&Context);
    DeleteFileW(Path);
    ok(ActCtx != INVALID_HANDLE_VALUE, "CreateActCtx failed with %lu\n", GetLastError());
    if (ActCtx != INVALID_HANDLE_VALUE && !ActivateActCtx(ActCtx, Cookie))
    {
        ok(FALSE, "ActivateActCtx failed with %lu\n", GetLastError());
        ReleaseActCtx(ActCtx);
        return INVALID_HANDLE_VALUE;
    }
    return ActCtx;
}

START_TEST(StatusBarTheme)
{
    INITCOMMONCONTROLSEX Controls = { sizeof(Controls), ICC_BAR_CLASSES };
    ULONG_PTR Cookie;
    HANDLE ActCtx;
    HWND Parent;

    if (!IsThemeActive())
    {
        skip("No visual style is active\n");
        return;
    }

    TestClass(L"ExplorerStatusBar", RGB(255, 255, 255), RGB(30, 57, 91));
    TestClass(L"DarkMode::ExplorerStatusBar", RGB(28, 28, 28), RGB(222, 222, 222));

    ActCtx = ActivateCommonControls6(&Cookie);
    if (ActCtx == INVALID_HANDLE_VALUE)
        return;
    ok(InitCommonControlsEx(&Controls), "InitCommonControlsEx failed\n");
    Parent = CreateWindowExW(0, L"STATIC", L"", WS_POPUP | WS_VISIBLE, 0, 0, 500, 120, NULL, NULL, NULL, NULL);
    ok(Parent != NULL, "parent window not created, error %lu\n", GetLastError());
    if (Parent)
    {
        TestRender(Parent, NULL, RGB(255, 255, 255));
        TestRender(Parent, L"DarkMode", RGB(28, 28, 28));
        DestroyWindow(Parent);
    }
    DeactivateActCtx(0, Cookie);
    ReleaseActCtx(ActCtx);
}
