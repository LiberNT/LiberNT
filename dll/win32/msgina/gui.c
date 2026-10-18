/*
 * PROJECT:         ReactOS msgina.dll
 * FILE:            dll/win32/msgina/gui.c
 * PURPOSE:         ReactOS Logon GINA DLL
 * PROGRAMMERS:     Hervé Poussineau (hpoussin@reactos.org)
 *                  Katayama Hirofumi MZ (katayama.hirofumi.mz@gmail.com)
 *                  Ahmed ARIF <arif.ing@outlook.com>
 */

#include "msgina.h"

#include <wingdi.h>
#include <reactos/bootstatus.h>
#include "logonui.h"

typedef struct _DISPLAYSTATUSMSG
{
    PGINA_CONTEXT Context;
    HDESK hDesktop;
    DWORD dwOptions;
    PWSTR pTitle;
    PWSTR pMessage;
    HANDLE StartupEvent;
} DISPLAYSTATUSMSG, *PDISPLAYSTATUSMSG;

static BOOL
GUIInitialize(
    IN OUT PGINA_CONTEXT pgContext)
{
    TRACE("GUIInitialize(%p)\n", pgContext);
    return TRUE;
}

static DWORD WINAPI
StartupWindowThread(LPVOID lpParam)
{
    HDESK hDesk = NULL;
    HWND Window = NULL;
    MSG Message;
    BOOL ReadySignaled = FALSE;
    PDISPLAYSTATUSMSG msg = (PDISPLAYSTATUSMSG)lpParam;

    /* When SetThreadDesktop is called the system closes the desktop handle when needed
       so we have to create a new handle because this handle may still be in use by winlogon  */
    if (!DuplicateHandle (  GetCurrentProcess(),
                            msg->hDesktop,
                            GetCurrentProcess(),
                            (HANDLE*)&hDesk,
                            0,
                            FALSE,
                            DUPLICATE_SAME_ACCESS))
    {
        ERR("Duplicating handle failed!\n");
        goto cleanup;
    }

    if (!SetThreadDesktop(hDesk))
    {
        ERR("Setting thread desktop failed!\n");
        CloseDesktop(hDesk);
        goto cleanup;
    }

    Window = BootStatusCreate(msg->pMessage);
    msg->Context->hStatusWindow = Window;
    SetEvent(msg->StartupEvent);
    ReadySignaled = TRUE;

    if (Window)
    {
        while (GetMessageW(&Message, NULL, 0, 0) > 0)
        {
            if (!Message.hwnd && Message.message == WM_HIDESTATUSCURSOR)
            {
                ShowCursor(FALSE);
                continue;
            }

            TranslateMessage(&Message);
            DispatchMessageW(&Message);
        }

        BootStatusDestroy(Window);
    }

cleanup:
    if (!ReadySignaled)
    {
        msg->Context->hStatusWindow = NULL;
        SetEvent(msg->StartupEvent);
    }
    HeapFree(GetProcessHeap(), 0, lpParam);
    return Window != NULL;
}

static BOOL
GUIDisplayStatusMessage(
    IN PGINA_CONTEXT pgContext,
    IN HDESK hDesktop,
    IN DWORD dwOptions,
    IN PWSTR pTitle,
    IN PWSTR pMessage)
{
    PDISPLAYSTATUSMSG msg;
    HANDLE Thread;
    HANDLE StartupEvent;

    TRACE("GUIDisplayStatusMessage(%ws)\n", pMessage);
    UNREFERENCED_PARAMETER(pTitle);

    if (LogonUiCoverShown())
        return TRUE;

    if (!pgContext->hStatusWindow)
    {
        /*
         * If everything goes correctly, 'msg' is freed
         * by the 'StartupWindowThread' thread.
         */
        msg = (PDISPLAYSTATUSMSG)HeapAlloc(GetProcessHeap(),
                                           HEAP_ZERO_MEMORY,
                                           sizeof(*msg));
        if(!msg)
            return FALSE;

        msg->Context = pgContext;
        msg->dwOptions = dwOptions;
        msg->pTitle = pTitle;
        msg->pMessage = pMessage;
        msg->hDesktop = hDesktop;

        msg->StartupEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

        if (!msg->StartupEvent)
        {
            HeapFree(GetProcessHeap(), 0, msg);
            return FALSE;
        }
        StartupEvent = msg->StartupEvent;

        Thread = CreateThread(NULL,
                              0,
                              StartupWindowThread,
                              (PVOID)msg,
                              0,
                              NULL);
        if (Thread)
        {
            /* 'msg' will be freed by 'StartupWindowThread' */

            CloseHandle(Thread);
            WaitForSingleObject(StartupEvent, INFINITE);
            CloseHandle(StartupEvent);
            return pgContext->hStatusWindow != NULL;
        }
        else
        {
            /*
             * The 'StartupWindowThread' thread couldn't be created,
             * so we need to free the allocated 'msg'.
             */
            CloseHandle(StartupEvent);
            HeapFree(GetProcessHeap(), 0, msg);
        }

        return FALSE;
    }

    return BootStatusSetText(pgContext->hStatusWindow, pMessage);
}

static BOOL
GUIRemoveStatusMessage(
    IN PGINA_CONTEXT pgContext)
{
    if (pgContext->hStatusWindow && !pgContext->bAutoAdminLogon)
    {
        SendMessageW(pgContext->hStatusWindow, WM_CLOSE, 0, 0);
        pgContext->hStatusWindow = NULL;
    }
    LogonUiRemoveCover();

    return TRUE;
}

static VOID
GUIDisplaySASNotice(
    IN OUT PGINA_CONTEXT pgContext)
{
    LogonUiRun(pgContext, LogonUiNotice);
}

static INT
GUILoggedOnSAS(
    IN OUT PGINA_CONTEXT pgContext,
    IN DWORD dwSasType)
{
    if (dwSasType != WLX_SAS_TYPE_CTRL_ALT_DEL)
        return WLX_SAS_ACTION_NONE;

    return LogonUiRun(pgContext, LogonUiOptions);
}

static INT
GUILoggedOutSAS(
    IN OUT PGINA_CONTEXT pgContext)
{
    return LogonUiRun(pgContext, LogonUiSignIn);
}

static INT
GUILockedSAS(
    IN OUT PGINA_CONTEXT pgContext)
{
    return LogonUiRun(pgContext, LogonUiUnlock);
}

static VOID
GUIDisplayLockedNotice(
    IN OUT PGINA_CONTEXT pgContext)
{
    LogonUiRun(pgContext, LogonUiLockedNotice);
}

GINA_UI GinaGraphicalUI = {
    GUIInitialize,
    GUIDisplayStatusMessage,
    GUIRemoveStatusMessage,
    GUIDisplaySASNotice,
    GUILoggedOnSAS,
    GUILoggedOutSAS,
    GUILockedSAS,
    GUIDisplayLockedNotice,
};
