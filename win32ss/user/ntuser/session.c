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
    NewSession->Next = gpUserSessions;
    WritePointerRelease((PVOID volatile *)&gpUserSessions, NewSession);
    *Session = NewSession;
    return STATUS_SUCCESS;
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

