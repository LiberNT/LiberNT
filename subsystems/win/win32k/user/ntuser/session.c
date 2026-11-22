/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS kernel
 * PURPOSE:          Session stuff
 * FILE:             win32ss/user/ntuser/session.c
 * PROGRAMER:        Gunnar
 */

#include <win32k.h>

/* GLOBALS *******************************************************************/

PRTL_ATOM_TABLE gAtomTable = NULL;
static PUSER_SESSION gpUserSessions = NULL;
PUSER_SESSION gpDisplaySession = NULL;
BOOLEAN gbConsoleSession = FALSE;

/* FUNCTIONS *******************************************************************/

NTSTATUS FASTCALL
InitSessionImpl(VOID)
{
   return RtlCreateAtomTable(37, &gAtomTable);
}

PUSER_SESSION FASTCALL
UserFindSession(ULONG SessionId)
{
    PUSER_SESSION Session;

    for (Session = ReadPointerAcquire((PVOID volatile *)&gpUserSessions); Session; Session = Session->Next)
    {
        if (Session->SessionId == SessionId)
            return Session;
    }
    return NULL;
}

PUSER_SESSION FASTCALL
UserGetProcessSession(PEPROCESS Process)
{
    ULONG SessionId = PsGetProcessSessionIdEx(Process);

    if (SessionId == (ULONG)-1)
        return ReadPointerAcquire((PVOID volatile *)&gpDisplaySession);
    return UserFindSession(SessionId);
}

PUSER_SESSION FASTCALL
UserGetCurrentSession(VOID)
{
    return UserGetProcessSession(PsGetCurrentProcess());
}

NTSTATUS FASTCALL
UserCreateSession(ULONG SessionId, PUSER_SESSION *Session)
{
    PUSER_SESSION NewSession;

    ASSERT(UserIsEnteredExclusive());

    NewSession = UserFindSession(SessionId);
    if (NewSession)
    {
        *Session = NewSession;
        return STATUS_SUCCESS;
    }

    NewSession = ExAllocatePoolZero(NonPagedPool, sizeof(*NewSession), TAG_SESSION);
    if (!NewSession)
        return STATUS_INSUFFICIENT_RESOURCES;

    NewSession->SessionId = SessionId;
    KeInitializeEvent(&NewSession->DesktopThreadStarted, SynchronizationEvent, FALSE);
    InitializeListHead(&NewSession->DesktopsToDestroy);
    NewSession->Next = gpUserSessions;
    WritePointerRelease((PVOID volatile *)&gpUserSessions, NewSession);
    *Session = NewSession;
    return STATUS_SUCCESS;
}

VOID FASTCALL
UserBeginSessionsEnd(VOID)
{
    PUSER_SESSION Session;

    ASSERT(UserIsEnteredExclusive());
    for (Session = ReadPointerAcquire((PVOID volatile *)&gpUserSessions); Session; Session = Session->Next)
        Session->Ending = TRUE;
}

VOID FASTCALL
UserReleaseSessions(VOID)
{
    PUSER_SESSION Session;

    for (Session = ReadPointerAcquire((PVOID volatile *)&gpUserSessions); Session; Session = Session->Next)
    {
        ASSERT(Session->Ending);
        ResetCsrApiPort(Session);
        ResetCsrProcess(Session);
        if (Session->DesktopSwitchEvent)
        {
            ObDereferenceObject(Session->DesktopSwitchEvent);
            Session->DesktopSwitchEvent = NULL;
            Session->DesktopSwitchEventHandle = NULL;
        }
        if (Session->WinStaDir)
        {
            ZwClose(Session->WinStaDir);
            Session->WinStaDir = NULL;
        }
    }
}

VOID FASTCALL
FreeSessionImpl(VOID)
{
    PUSER_SESSION Session;

    WritePointerRelease((PVOID volatile *)&gpDisplaySession, NULL);
    while ((Session = gpUserSessions) != NULL)
    {
        ASSERT(Session->Ending);
        ASSERT(Session->CsrProcess == NULL && Session->CsrApiPort == NULL);
        ASSERT(Session->WinStaDir == NULL && Session->DesktopSwitchEvent == NULL);
        WritePointerRelease((PVOID volatile *)&gpUserSessions, Session->Next);
        RtlFreeUnicodeString(&Session->WinStaDirName);
        ExFreePoolWithTag(Session, TAG_SESSION);
    }
    if (gAtomTable)
    {
        RtlDestroyAtomTable(gAtomTable);
        gAtomTable = NULL;
    }
}

PUNICODE_STRING FASTCALL
UserGetWinStaDirName(PEPROCESS Process)
{
    static UNICODE_STRING EmptyName = RTL_CONSTANT_STRING(L"");
    PUSER_SESSION Session = UserGetProcessSession(Process);

    if (!Session)
        Session = ReadPointerAcquire((PVOID volatile *)&gpDisplaySession);
    return Session ? &Session->WinStaDirName : &EmptyName;
}

PWINSTATION_OBJECT FASTCALL
UserGetInputWindowStation(PEPROCESS Process)
{
    PUSER_SESSION Session = UserGetProcessSession(Process);

    return Session ? Session->InputWindowStation : NULL;
}

PWINSTATION_OBJECT FASTCALL
UserGetDisplayInputWindowStation(VOID)
{
    PUSER_SESSION Session = ReadPointerAcquire((PVOID volatile *)&gpDisplaySession);

    return Session ? Session->InputWindowStation : NULL;
}

BOOLEAN FASTCALL
UserIsCsrProcess(PEPROCESS Process)
{
    PUSER_SESSION Session;

    for (Session = ReadPointerAcquire((PVOID volatile *)&gpUserSessions); Session; Session = Session->Next)
    {
        if (Session->CsrProcess == Process)
            return TRUE;
    }
    return FALSE;
}

PUSER_SESSION FASTCALL
UserGetDesktopThreadSession(PTHREADINFO pti)
{
    PUSER_SESSION Session;

    if (!pti)
        return NULL;
    for (Session = ReadPointerAcquire((PVOID volatile *)&gpUserSessions); Session; Session = Session->Next)
    {
        if (Session->DesktopThread == pti)
            return Session;
    }
    return NULL;
}

BOOLEAN FASTCALL
UserIsDesktopThread(PTHREADINFO pti)
{
    return UserGetDesktopThreadSession(pti) != NULL;
}

PTHREADINFO FASTCALL
UserGetDesktopThread(VOID)
{
    PUSER_SESSION Session = UserGetCurrentSession();

    return Session ? Session->DesktopThread : NULL;
}

BOOLEAN FASTCALL
UserIsLogonProcess(HANDLE ProcessId)
{
    PUSER_SESSION Session;

    if (!ProcessId)
        return FALSE;
    for (Session = ReadPointerAcquire((PVOID volatile *)&gpUserSessions); Session; Session = Session->Next)
    {
        if (Session->LogonPid == ProcessId)
            return TRUE;
    }
    return FALSE;
}

