/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that the install mode task dialog of Inno Setup fits its client area
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "appsmoke.h"
#include <commctrl.h>

typedef HRESULT (WINAPI *TASK_DIALOG_INDIRECT)(const TASKDIALOGCONFIG *, int *, int *, BOOL *);

static const WCHAR LinkMe[] = L"Install for &me only (recommended)";
static const WCHAR LinkAll[] = L"Install for &all users";

typedef struct _TASK_DIALOG_LAYOUT
{
    HWND Dialog;
    RECT Client;
    HWND Buttons[3];
    RECT Rects[3];
    BOOL Measured;
} TASK_DIALOG_LAYOUT;

static BOOL CALLBACK FindButton(HWND Window, LPARAM Param)
{
    TASK_DIALOG_LAYOUT *Layout = (TASK_DIALOG_LAYOUT *)Param;
    WCHAR Class[32], Text[64];

    if (!GetClassNameW(Window, Class, ARRAYSIZE(Class)) || lstrcmpiW(Class, L"Button"))
        return TRUE;
    GetWindowTextW(Window, Text, ARRAYSIZE(Text));
    if (!lstrcmpW(Text, LinkMe))
        Layout->Buttons[0] = Window;
    else if (!lstrcmpW(Text, LinkAll))
        Layout->Buttons[1] = Window;
    else if (GetDlgCtrlID(Window) == IDCANCEL || !lstrcmpW(Text, L"Cancel"))
        Layout->Buttons[2] = Window;
    return TRUE;
}

static void MeasureTaskDialog(TASK_DIALOG_LAYOUT *Layout, HWND Dialog)
{
    UINT Index;

    Layout->Dialog = Dialog;
    GetClientRect(Dialog, &Layout->Client);
    EnumChildWindows(Dialog, FindButton, (LPARAM)Layout);
    for (Index = 0; Index < ARRAYSIZE(Layout->Buttons); Index++)
    {
        if (!Layout->Buttons[Index])
            continue;
        GetWindowRect(Layout->Buttons[Index], &Layout->Rects[Index]);
        MapWindowPoints(NULL, Dialog, (POINT *)&Layout->Rects[Index], 2);
    }
    Layout->Measured = TRUE;
}

static void CheckTaskDialog(const TASK_DIALOG_LAYOUT *Layout)
{
    static const char *Names[] = { "first command link", "second command link", "Cancel button" };
    const RECT *Client = &Layout->Client;
    SIZE Ideal;
    UINT Index;

    trace("Client %ldx%ld\n", Client->right, Client->bottom);
    for (Index = 0; Index < ARRAYSIZE(Layout->Buttons); Index++)
    {
        const RECT *Rect = &Layout->Rects[Index];

        ok(Layout->Buttons[Index] != NULL, "The %s was not found\n", Names[Index]);
        if (!Layout->Buttons[Index])
            return;
        trace("%s %ld,%ld,%ld,%ld\n", Names[Index], Rect->left, Rect->top, Rect->right, Rect->bottom);
        ok(Rect->left >= 0 && Rect->top >= 0 && Rect->right <= Client->right && Rect->bottom <= Client->bottom,
           "The %s (%ld,%ld,%ld,%ld) does not fit the %ldx%ld client area\n", Names[Index],
           Rect->left, Rect->top, Rect->right, Rect->bottom, Client->right, Client->bottom);
    }

    ok(Layout->Rects[0].right == Layout->Rects[2].right && Layout->Rects[1].right == Layout->Rects[2].right,
       "The command links end at %ld and %ld, the Cancel button at %ld\n",
       Layout->Rects[0].right, Layout->Rects[1].right, Layout->Rects[2].right);
    ok(Layout->Rects[1].top == Layout->Rects[0].bottom,
       "The second command link starts at %ld, the first ends at %ld\n", Layout->Rects[1].top, Layout->Rects[0].bottom);

    for (Index = 0; Index < 2; Index++)
    {
        Ideal.cx = Layout->Rects[Index].right - Layout->Rects[Index].left;
        Ideal.cy = 0;
        ok(SendMessageW(Layout->Buttons[Index], BCM_GETIDEALSIZE, 0, (LPARAM)&Ideal),
           "BCM_GETIDEALSIZE failed for the %s\n", Names[Index]);
        ok(Layout->Rects[Index].bottom - Layout->Rects[Index].top >= Ideal.cy,
           "The %s is %ld pixels high, its text needs %ld\n", Names[Index],
           Layout->Rects[Index].bottom - Layout->Rects[Index].top, Ideal.cy);
        ok(Layout->Rects[Index].bottom - Layout->Rects[Index].top >= Layout->Rects[2].bottom - Layout->Rects[2].top,
           "The %s is %ld pixels high, the Cancel button %ld\n", Names[Index],
           Layout->Rects[Index].bottom - Layout->Rects[Index].top, Layout->Rects[2].bottom - Layout->Rects[2].top);
    }
}

static HRESULT CALLBACK TaskDialogCallback(HWND Dialog, UINT Notification, WPARAM wParam, LPARAM lParam, LONG_PTR Data)
{
    TASK_DIALOG_LAYOUT *Layout = (TASK_DIALOG_LAYOUT *)Data;

    if (Notification == TDN_CREATED)
    {
        SendMessageW(Dialog, TDM_SET_BUTTON_ELEVATION_REQUIRED_STATE, IDNO, TRUE);
    }
    else if (Notification == TDN_TIMER && !Layout->Measured)
    {
        MeasureTaskDialog(Layout, Dialog);
        CheckTaskDialog(Layout);
        SendMessageW(Dialog, TDM_CLICK_BUTTON, IDCANCEL, 0);
    }
    return S_OK;
}

static HANDLE ActivateCommonControls6(ULONG_PTR *Cookie, WCHAR *Path)
{
    static const char Manifest[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
        "<dependency><dependentAssembly><assemblyIdentity type=\"win32\" name=\"Microsoft.Windows.Common-Controls\""
        " version=\"6.0.0.0\" processorArchitecture=\"*\" publicKeyToken=\"6595b64144ccf1df\" language=\"*\"/>"
        "</dependentAssembly></dependency></assembly>";
    WCHAR Directory[MAX_PATH];
    ACTCTXW Context = { sizeof(Context) };
    HANDLE File, ActivationContext;
    DWORD Written;

    if (!GetTempPathW(ARRAYSIZE(Directory), Directory) || !GetTempFileNameW(Directory, L"tdm", 0, Path))
        return INVALID_HANDLE_VALUE;
    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    WriteFile(File, Manifest, sizeof(Manifest) - 1, &Written, NULL);
    CloseHandle(File);

    Context.lpSource = Path;
    ActivationContext = CreateActCtxW(&Context);
    if (ActivationContext != INVALID_HANDLE_VALUE && !ActivateActCtx(ActivationContext, Cookie))
    {
        ReleaseActCtx(ActivationContext);
        ActivationContext = INVALID_HANDLE_VALUE;
    }
    return ActivationContext;
}

START_TEST(task_dialog)
{
    TASK_DIALOG_LAYOUT Layout = { 0 };
    TASKDIALOG_BUTTON Buttons[2];
    TASKDIALOGCONFIG Config = { sizeof(Config) };
    TASK_DIALOG_INDIRECT TaskDialogIndirectPtr;
    WCHAR ManifestPath[MAX_PATH];
    ULONG_PTR Cookie = 0;
    HANDLE ActivationContext;
    HMODULE ComCtl32;
    HRESULT Result;
    int Button = 0;

    ActivationContext = ActivateCommonControls6(&Cookie, ManifestPath);
    ok(ActivationContext != INVALID_HANDLE_VALUE, "Activating Common Controls 6 failed: %lu\n", GetLastError());
    if (ActivationContext == INVALID_HANDLE_VALUE)
        return;

    ComCtl32 = LoadLibraryW(L"comctl32.dll");
    TaskDialogIndirectPtr = ComCtl32 ? (TASK_DIALOG_INDIRECT)GetProcAddress(ComCtl32, "TaskDialogIndirect") : NULL;
    ok(TaskDialogIndirectPtr != NULL, "TaskDialogIndirect is not exported by Common Controls 6\n");
    if (TaskDialogIndirectPtr)
    {
        Buttons[0].nButtonID = IDYES;
        Buttons[0].pszButtonText = LinkMe;
        Buttons[1].nButtonID = IDNO;
        Buttons[1].pszButtonText = LinkAll;
        Config.dwFlags = TDF_USE_COMMAND_LINKS | TDF_CALLBACK_TIMER;
        Config.hInstance = GetModuleHandleW(NULL);
        Config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
        Config.pszWindowTitle = L"Select Setup Install Mode";
        Config.pszMainIcon = TD_INFORMATION_ICON;
        Config.pszMainInstruction = L"Select install mode";
        Config.pszContent = L"GIMP can be installed for you only, or for all users (requires administrative privileges).";
        Config.pButtons = Buttons;
        Config.cButtons = ARRAYSIZE(Buttons);
        Config.pfCallback = TaskDialogCallback;
        Config.lpCallbackData = (LONG_PTR)&Layout;

        Result = TaskDialogIndirectPtr(&Config, &Button, NULL, NULL);
        ok(Result == S_OK, "TaskDialogIndirect returned 0x%08lx\n", Result);
        ok(Button == IDCANCEL, "TaskDialogIndirect returned button %d\n", Button);
        ok(Layout.Measured, "The task dialog was never laid out\n");
    }

    if (ComCtl32)
        FreeLibrary(ComCtl32);
    DeactivateActCtx(0, Cookie);
    ReleaseActCtx(ActivationContext);
    DeleteFileW(ManifestPath);
}
