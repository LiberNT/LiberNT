/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/t_session.c
 * PURPOSE:     Session memory host-native regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "mmharness.h"
#include "ntsessionshim.h"

extern PVOID MmSessionBase;
extern SIZE_T MmSessionSize;

PMI_SYSTEM MiSessionTestSystem;
SESSION_TEST_UNLOAD MiSessionTestUnload;
_Thread_local PEPROCESS MiSessionTestCurrent;
volatile LONG MiSessionTestAllocations;
volatile LONG MiSessionTestFailAllocate;
LCID PsDefaultThreadLocaleId = 0x409, PsDefaultSystemLocaleId = 0x409;

static void
SessionProcessInitialize(PEPROCESS Process, PMI_PROCESS Native)
{
    RtlZeroMemory(Process, sizeof(*Process));
    RtlZeroMemory(Native, sizeof(*Native));
    Process->Pcb.Owner = Process;
    Process->References = 1;
    Process->AddressSpaceInitialized = 2;
    Process->Vm.VmWorkingSetList = Native;
    CHECK(NT_SUCCESS(MiAddressSpaceCreate(MiSessionTestSystem, &Native->Space)));
    CHECK(Native->Space.RootFrame != MI_FRAME_INVALID && Native->Space.RootFrame != 0);
}

static void
SessionLifecycle(void)
{
    struct _EPROCESS_HOST Leader, Child, Outsider;
    MI_PROCESS Native[3];
    ULONG Id, NextId;
    PVOID Session;
    KAPC_STATE State;
    NTSTATUS Status;

    SessionProcessInitialize(&Leader, &Native[0]);
    SessionProcessInitialize(&Child, &Native[1]);
    SessionProcessInitialize(&Outsider, &Native[2]);
    MiSessionTestCurrent = &Leader;
    CHECK(MmSessionCreate(&Id) == STATUS_SUCCESS);
    Status = MmSessionDelete(Id + 1);
    CHECK(Status == STATUS_INVALID_PARAMETER);
    if (Status != STATUS_INVALID_PARAMETER)
        return;
    CHECK(MmSessionCreate(&NextId) == STATUS_ALREADY_COMMITTED);
    MiSessionAddProcess(&Child);
    CHECK(MmGetSessionId(&Child) == Id && Child.Session == Leader.Session);
    Session = MmGetSessionById(Id);
    CHECK(Session != NULL);
    MiSessionTestCurrent = &Outsider;
    Child.AddressSpaceInitialized = 1;
    CHECK(MmAttachSession(Session, &State) == STATUS_PROCESS_IS_TERMINATING);
    CHECK(MiSessionTestCurrent == &Outsider && Child.References == 1);
    Child.AddressSpaceInitialized = 2;
    CHECK(MmAttachSession(Session, &State) == STATUS_SUCCESS);
    CHECK(MiSessionTestCurrent == &Child);
    CHECK(Child.References == 2 && Child.RundownProtect.State == 2);
    MmSetSessionLocaleId(0x40C);
    CHECK(MmGetSessionLocaleId() == 0x40C);
    MmDetachSession(Session, &State);
    CHECK(MiSessionTestCurrent == &Outsider);
    CHECK(Child.References == 1 && Child.RundownProtect.State == 0);
    MiSessionTestCurrent = &Leader;
    CHECK(MmSessionDelete(Id) == STATUS_SUCCESS);
    CHECK(Leader.Session == NULL && !(Leader.Flags & PSF_PROCESS_IN_SESSION_BIT));
    CHECK(Child.Session == Session && MmGetSessionId(&Child) == Id);
    CHECK(MmSessionDelete(Id) == STATUS_UNABLE_TO_FREE_VM);
    CHECK(MmSessionCreate(&NextId) == STATUS_SUCCESS && NextId != Id);
    CHECK(MmSessionDelete(NextId) == STATUS_SUCCESS);
    MiSessionRemoveProcess(&Child);
    CHECK(Child.Session == NULL && !(Child.Flags & PSF_PROCESS_IN_SESSION_BIT));
    CHECK(MmGetSessionById(Id) == NULL);
    CHECK(MmAttachSession(Session, &State) == STATUS_PROCESS_IS_TERMINATING);
    MmQuitNextSession(Session);
    CHECK(MiSessionTestAllocations == 0);
    MiSessionRemoveProcess(&Leader);
    CHECK(!Leader.Vm.Flags.SessionLeader);

    MiSessionTestCurrent = &Outsider;
    MiSessionTestFailAllocate = TRUE;
    CHECK(MmSessionCreate(&Id) == STATUS_NO_MEMORY);
    CHECK(Outsider.Session == NULL && !Outsider.Vm.Flags.SessionLeader);
    MiSessionTestFailAllocate = FALSE;
    CHECK(MmSessionCreate(&Id) == STATUS_SUCCESS);
    MiSessionRemoveProcess(&Outsider);
    CHECK(Outsider.Session == NULL && MiSessionTestAllocations == 0);
    MiSessionRemoveProcess(&Outsider);
    CHECK(MiSessionTestAllocations == 0);
    MiSessionTestCurrent = NULL;
}

typedef struct _SESSION_THREAD
{
    struct _EPROCESS_HOST Process;
    MI_PROCESS Native;
    PVOID Session;
    PEPROCESS Target;
    volatile LONG *Progress;
    NTSTATUS Status;
    ULONG Id;
} SESSION_THREAD;

static void *
SessionCreateThread(void *Argument)
{
    SESSION_THREAD *Thread = Argument;

    MiSessionTestCurrent = &Thread->Process;
    Thread->Status = MmSessionCreate(&Thread->Id);
    return NULL;
}

static void
SessionCreateRace(void)
{
    SESSION_THREAD Args[4];
    pthread_t Threads[4];
    ULONG i, Winners = 0;

    for (i = 0; i < 4; i++)
    {
        SessionProcessInitialize(&Args[i].Process, &Args[i].Native);
        CHECK(pthread_create(&Threads[i], NULL, SessionCreateThread, &Args[i]) == 0);
    }
    for (i = 0; i < 4; i++)
        CHECK(pthread_join(Threads[i], NULL) == 0);
    for (i = 0; i < 4; i++)
    {
        CHECK(Args[i].Status == STATUS_SUCCESS || Args[i].Status == STATUS_INVALID_SYSTEM_SERVICE);
        if (Args[i].Status == STATUS_SUCCESS)
            Winners++;
        MiSessionRemoveProcess(&Args[i].Process);
    }
    CHECK(Winners == 1 && MiSessionTestAllocations == 0);
}

static void *
SessionAttachThread(void *Argument)
{
    SESSION_THREAD *Thread = Argument;
    KAPC_STATE State;
    ULONG i;

    MiSessionTestCurrent = &Thread->Process;
    for (i = 0; i < 256; i++)
    {
        NTSTATUS Status = MmAttachSession(Thread->Session, &State);

        CHECK(Status == STATUS_SUCCESS || Status == STATUS_PROCESS_IS_TERMINATING);
        if (NT_SUCCESS(Status))
        {
            CHECK(MiSessionTestCurrent == Thread->Target);
            CHECK(MmGetSessionId(MiSessionTestCurrent) == Thread->Id);
            MmDetachSession(Thread->Session, &State);
        }
        CHECK(MiSessionTestCurrent == &Thread->Process);
        InterlockedIncrement(Thread->Progress);
    }
    return NULL;
}

static void
SessionAttachRundown(void)
{
    struct _EPROCESS_HOST Leader, Child;
    MI_PROCESS Native[2];
    SESSION_THREAD Args[4];
    pthread_t Threads[4];
    volatile LONG Progress = 0;
    PVOID Session;
    ULONG Id, i;

    SessionProcessInitialize(&Leader, &Native[0]);
    SessionProcessInitialize(&Child, &Native[1]);
    MiSessionTestCurrent = &Leader;
    CHECK(MmSessionCreate(&Id) == STATUS_SUCCESS);
    MiSessionAddProcess(&Child);
    MiSessionAddProcess(&Child);
    Session = MmGetSessionById(Id);
    CHECK(Session != NULL);
    for (i = 0; i < 4; i++)
    {
        SessionProcessInitialize(&Args[i].Process, &Args[i].Native);
        Args[i].Session = Session;
        Args[i].Target = &Child;
        Args[i].Id = Id;
        Args[i].Progress = &Progress;
        CHECK(pthread_create(&Threads[i], NULL, SessionAttachThread, &Args[i]) == 0);
    }
    while (MI_ATOMIC_READ32(&Progress) < 16)
        MI_PAUSE();
    __atomic_fetch_or(&Child.RundownProtect.State, 1, __ATOMIC_SEQ_CST);
    while (MI_ATOMIC_READ32(&Child.RundownProtect.State) != 1)
        MI_PAUSE();
    MiSessionRemoveProcess(&Child);
    CHECK(MmSessionDelete(Id) == STATUS_SUCCESS);
    for (i = 0; i < 4; i++)
        CHECK(pthread_join(Threads[i], NULL) == 0);
    CHECK(Progress == 1024 && Child.References == 1 && Child.RundownProtect.State == 1);
    CHECK(MmGetSessionById(Id) == NULL);
    MmQuitNextSession(Session);
    MiSessionRemoveProcess(&Leader);
    CHECK(MiSessionTestAllocations == 0);
    MiSessionTestCurrent = NULL;
}

static void
SessionSetKeepsProcess(void)
{
    struct _EPROCESS_HOST Leader, Keeper0, Keeper1, Mover;
    MI_PROCESS Native[4];
    ULONG Id0, Id1;
    PVOID Session0;

    SessionProcessInitialize(&Leader, &Native[0]);
    SessionProcessInitialize(&Keeper0, &Native[1]);
    SessionProcessInitialize(&Keeper1, &Native[2]);
    SessionProcessInitialize(&Mover, &Native[3]);
    MiSessionTestCurrent = &Leader;
    CHECK(MmSessionCreate(&Id0) == STATUS_SUCCESS);
    MiSessionAddProcess(&Keeper0);
    MiSessionAddProcess(&Mover);
    CHECK(MmSessionDelete(Id0) == STATUS_SUCCESS);
    CHECK(MmSessionCreate(&Id1) == STATUS_SUCCESS && Id1 != Id0);
    MiSessionAddProcess(&Keeper1);
    CHECK(MmSessionDelete(Id1) == STATUS_SUCCESS);
    Session0 = Mover.Session;

    CHECK(MmSetProcessSession(&Mover, Id0) == STATUS_SUCCESS && Mover.Session == Session0);
    CHECK(MmSetProcessSession(&Mover, Id1) == STATUS_ACCESS_DENIED);
    CHECK(MmGetSessionId(&Mover) == Id0 && Mover.Session == Session0);
    CHECK((Mover.Flags & PSF_PROCESS_IN_SESSION_BIT) && Keeper0.Session == Session0 && Keeper1.Session != Session0);
    CHECK(MmSetProcessSession(&Mover, Id1 + 1) == STATUS_ACCESS_DENIED);
    CHECK(MmGetSessionId(&Mover) == Id0);
    CHECK(MmSetProcessSession(&Leader, Id1) == STATUS_ACCESS_DENIED);
    CHECK(MmSetProcessSession(&Leader, MmGetSessionId(&Leader)) == STATUS_SUCCESS);
    CHECK(MmSetProcessSession(&Keeper1, Id1) == STATUS_SUCCESS && MmGetSessionId(&Keeper1) == Id1);

    MiSessionRemoveProcess(&Keeper0);
    CHECK(MmGetSessionId(&Mover) == Id0);
    MiSessionRemoveProcess(&Mover);
    CHECK(MmSetProcessSession(&Keeper1, Id0) == STATUS_ACCESS_DENIED);
    CHECK(MmGetSessionId(&Keeper1) == Id1);
    MiSessionRemoveProcess(&Keeper1);
    MiSessionRemoveProcess(&Leader);
    CHECK(MiSessionTestAllocations == 0);
    MiSessionTestCurrent = NULL;
}

static void
SessionMoveProcess(void)
{
    struct _EPROCESS_HOST Leader, Keeper0, Keeper1, Mover;
    MI_PROCESS Native[4];
    ULONG Id0, Id1;
    PVOID Session0, Session1;

    SessionProcessInitialize(&Leader, &Native[0]);
    SessionProcessInitialize(&Keeper0, &Native[1]);
    SessionProcessInitialize(&Keeper1, &Native[2]);
    SessionProcessInitialize(&Mover, &Native[3]);
    MiSessionTestCurrent = &Leader;
    CHECK(MmSessionCreate(&Id0) == STATUS_SUCCESS);
    MiSessionAddProcess(&Keeper0);
    MiSessionAddProcess(&Mover);
    CHECK(MmSessionDelete(Id0) == STATUS_SUCCESS);
    CHECK(MmSessionCreate(&Id1) == STATUS_SUCCESS && Id1 != Id0);
    MiSessionAddProcess(&Keeper1);
    CHECK(MmSessionDelete(Id1) == STATUS_SUCCESS);
    Session0 = Mover.Session;
    Session1 = Keeper1.Session;
    CHECK(Session0 != NULL && Session1 != NULL && Session0 != Session1);

    CHECK(MmMoveProcessToSession(&Mover, Id0) == STATUS_SUCCESS && Mover.Session == Session0);
    CHECK(MmMoveProcessToSession(&Mover, Id1) == STATUS_SUCCESS);
    CHECK(MmGetSessionId(&Mover) == Id1 && Mover.Session == Session1);
    CHECK((Mover.Flags & PSF_PROCESS_IN_SESSION_BIT) && Keeper0.Session == Session0 && Keeper1.Session == Session1);
    CHECK(MmSetProcessSession(&Mover, Id1) == STATUS_SUCCESS);
    CHECK(MmSetProcessSession(&Mover, Id0) == STATUS_ACCESS_DENIED && Mover.Session == Session1);
    CHECK(MmMoveProcessToSession(&Mover, Id1 + 1) == STATUS_INVALID_PARAMETER);
    CHECK(MmGetSessionId(&Mover) == Id1 && Mover.Session == Session1);
    CHECK(MmMoveProcessToSession(&Leader, Id1) == STATUS_INVALID_PARAMETER && Leader.Session == NULL);
    CHECK(MmMoveProcessToSession(&Mover, Id0) == STATUS_SUCCESS);
    CHECK(MmGetSessionId(&Mover) == Id0 && Mover.Session == Session0);

    MiSessionRemoveProcess(&Keeper0);
    CHECK(MmGetSessionId(&Mover) == Id0);
    CHECK(MmMoveProcessToSession(&Mover, Id1) == STATUS_SUCCESS);
    CHECK(MmGetSessionId(&Mover) == Id1 && Mover.Session == Session1);
    CHECK(MmGetSessionById(Id0) == NULL);
    CHECK(MmMoveProcessToSession(&Keeper1, Id0) == STATUS_INVALID_PARAMETER);
    CHECK(MmGetSessionId(&Keeper1) == Id1 && Keeper1.Session == Session1);
    MiSessionRemoveProcess(&Mover);
    CHECK(Mover.Session == NULL && !(Mover.Flags & PSF_PROCESS_IN_SESSION_BIT));
    MiSessionRemoveProcess(&Keeper1);
    MiSessionRemoveProcess(&Leader);
    CHECK(MiSessionTestAllocations == 0);
    MiSessionTestCurrent = NULL;
}

static void
SessionDriverUnload(void)
{
    struct _EPROCESS_HOST Leader, First, Last, Outsider;
    MI_PROCESS Native[4];
    ULONG Driver1, Driver2;
    ULONG Id;

    SessionProcessInitialize(&Leader, &Native[0]);
    SessionProcessInitialize(&First, &Native[1]);
    SessionProcessInitialize(&Last, &Native[2]);
    SessionProcessInitialize(&Outsider, &Native[3]);
    RtlZeroMemory(&MiSessionTestUnload, sizeof(MiSessionTestUnload));

    MiSessionTestCurrent = &Outsider;
    CHECK(MmSessionAddDriver(&Driver1) == STATUS_NOT_FOUND);
    MiSessionTestCurrent = &Leader;
    CHECK(MmSessionCreate(&Id) == STATUS_SUCCESS);
    CHECK(MmSessionAddDriver(&Driver2) == STATUS_SUCCESS);
    MiSessionAddProcess(&First);
    MiSessionAddProcess(&Last);
    CHECK(MmSessionDelete(Id) == STATUS_SUCCESS);
    CHECK(MiSessionTestUnload.Count == 0);
    MiSessionTestCurrent = &First;
    CHECK(MmSessionAddDriver(&Driver1) == STATUS_SUCCESS);
    MiSessionTestFailAllocate = TRUE;
    CHECK(MmSessionAddDriver(&Driver2) == STATUS_INSUFFICIENT_RESOURCES);
    MiSessionTestFailAllocate = FALSE;
    CHECK(MmSessionAddDriver(&Driver2) == STATUS_SUCCESS);

    MiSessionProcessCleaned(&First);
    CHECK(MiSessionTestUnload.Count == 0);
    MiSessionProcessCleaned(&First);
    CHECK(MiSessionTestUnload.Count == 0);
    CHECK(MmSessionAddDriver(&Driver1) == STATUS_SUCCESS);
    MiSessionTestCurrent = &Outsider;
    MiSessionProcessCleaned(&Last);
    CHECK(MiSessionTestUnload.Count == 4);
    CHECK(MiSessionTestUnload.LastDriver == &Driver1 && MiSessionTestUnload.LastProcess == &Last);
    CHECK(MiSessionTestCurrent == &Outsider);
    MiSessionTestCurrent = &Last;
    CHECK(MmSessionAddDriver(&Driver2) == STATUS_PROCESS_IS_TERMINATING);
    MiSessionProcessCleaned(&Last);
    CHECK(MiSessionTestUnload.Count == 4);
    CHECK(MmMoveProcessToSession(&Outsider, Id) == STATUS_INVALID_PARAMETER);
    MiSessionTestCurrent = &Outsider;
    MiSessionRemoveProcess(&First);
    MiSessionRemoveProcess(&Last);
    CHECK(MiSessionTestUnload.Count == 4 && MmGetSessionById(Id) == NULL);

    RtlZeroMemory(&MiSessionTestUnload, sizeof(MiSessionTestUnload));
    MiSessionTestCurrent = &Leader;
    CHECK(MmSessionCreate(&Id) == STATUS_SUCCESS);
    CHECK(MmSessionAddDriver(&Driver1) == STATUS_SUCCESS);
    CHECK(MmSessionDelete(Id) == STATUS_SUCCESS);
    CHECK(MiSessionTestUnload.Count == 1 && MiSessionTestUnload.LastProcess == &Leader);
    CHECK(Leader.Session == NULL && MmGetSessionById(Id) == NULL);

    RtlZeroMemory(&MiSessionTestUnload, sizeof(MiSessionTestUnload));
    SessionProcessInitialize(&First, &Native[1]);
    MiSessionTestCurrent = &Leader;
    CHECK(MmSessionCreate(&Id) == STATUS_SUCCESS);
    MiSessionAddProcess(&First);
    CHECK(MmSessionDelete(Id) == STATUS_SUCCESS);
    MiSessionTestCurrent = &First;
    CHECK(MmSessionAddDriver(&Driver1) == STATUS_SUCCESS);
    MiSessionTestCurrent = &Outsider;
    MiSessionRemoveProcess(&First);
    CHECK(MiSessionTestUnload.Count == 1 && MiSessionTestUnload.LastProcess == &First);
    CHECK(MiSessionTestCurrent == &Outsider && First.Session == NULL);

    MiSessionRemoveProcess(&Leader);
    MiSessionRemoveProcess(&Outsider);
    CHECK(MiSessionTestAllocations == 0);
    MiSessionTestCurrent = NULL;
}

static void
SessionSetBeforeAnySession(void)
{
    struct _EPROCESS_HOST Process;
    MI_PROCESS Native;

    SessionProcessInitialize(&Process, &Native);
    CHECK(MmGetSessionById(0) == NULL);
    CHECK(MmGetSessionId(&Process) == 0);
    CHECK(MmSetProcessSession(&Process, 0) == STATUS_SUCCESS);
    CHECK(Process.Session == NULL && MmGetSessionId(&Process) == 0);
    CHECK(MmSetProcessSession(&Process, 1) == STATUS_ACCESS_DENIED);
    CHECK(Process.Session == NULL);
    CHECK(MiSessionTestAllocations == 0);
}

static void
SessionMoveLastProcess(void)
{
    struct _EPROCESS_HOST Leader, Keeper, Mover, Caller;
    MI_PROCESS Native[4];
    ULONG Driver;
    ULONG Id0, Id1;

    SessionProcessInitialize(&Leader, &Native[0]);
    SessionProcessInitialize(&Keeper, &Native[1]);
    SessionProcessInitialize(&Mover, &Native[2]);
    SessionProcessInitialize(&Caller, &Native[3]);
    RtlZeroMemory(&MiSessionTestUnload, sizeof(MiSessionTestUnload));
    MiSessionTestCurrent = &Leader;
    CHECK(MmSessionCreate(&Id0) == STATUS_SUCCESS);
    MiSessionAddProcess(&Mover);
    CHECK(MmSessionDelete(Id0) == STATUS_SUCCESS);
    CHECK(MmSessionCreate(&Id1) == STATUS_SUCCESS);
    MiSessionAddProcess(&Keeper);
    CHECK(MmSessionDelete(Id1) == STATUS_SUCCESS);
    MiSessionTestCurrent = &Mover;
    CHECK(MmSessionAddDriver(&Driver) == STATUS_SUCCESS);

    MiSessionTestCurrent = &Caller;
    CHECK(MmMoveProcessToSession(&Mover, Id1) == STATUS_SUCCESS);
    CHECK(MiSessionTestUnload.Count == 1 && MiSessionTestUnload.LastProcess == &Mover);
    CHECK(MiSessionTestCurrent == &Caller && MmGetSessionId(&Mover) == Id1);
    CHECK(MmGetSessionById(Id0) == NULL);
    CHECK(MmMoveProcessToSession(&Keeper, Id0) == STATUS_INVALID_PARAMETER);

    MiSessionProcessCleaned(&Keeper);
    CHECK(MiSessionTestUnload.Count == 1);
    MiSessionProcessCleaned(&Mover);
    CHECK(MiSessionTestUnload.Count == 1);
    MiSessionRemoveProcess(&Keeper);
    MiSessionRemoveProcess(&Mover);
    MiSessionRemoveProcess(&Leader);
    MiSessionRemoveProcess(&Caller);
    CHECK(MiSessionTestAllocations == 0);
    MiSessionTestCurrent = NULL;
}

static void
SessionMultipleMembers(void)
{
    struct _EPROCESS_HOST Leader, Members[12];
    MI_PROCESS LeaderNative, Native[12];
    ULONG Id, i;
    PVOID Session;

    SessionProcessInitialize(&Leader, &LeaderNative);
    MiSessionTestCurrent = &Leader;
    CHECK(MmSessionCreate(&Id) == STATUS_SUCCESS);
    Session = MmGetSessionById(Id);
    CHECK(Session != NULL);
    for (i = 0; i < RTL_NUMBER_OF(Members); i++)
    {
        SessionProcessInitialize(&Members[i], &Native[i]);
        MiSessionAddProcess(&Members[i]);
        CHECK(Members[i].Session == Session && MmGetSessionId(&Members[i]) == Id);
    }
    for (i = 1; i < RTL_NUMBER_OF(Members); i += 2)
    {
        MiSessionRemoveProcess(&Members[i]);
        CHECK(Members[i].Session == NULL && !(Members[i].Flags & PSF_PROCESS_IN_SESSION_BIT));
    }
    CHECK(MmSessionDelete(Id) == STATUS_SUCCESS);
    for (i = 0; i < RTL_NUMBER_OF(Members); i += 2)
    {
        CHECK(MmGetSessionId(&Members[i]) == Id);
        MiSessionRemoveProcess(&Members[i]);
    }
    CHECK(MmGetSessionById(Id) == NULL);
    MmQuitNextSession(Session);
    MiSessionRemoveProcess(&Leader);
    CHECK(MiSessionTestAllocations == 0);
    MiSessionTestCurrent = NULL;
}

static void
SessionSpaceRange(void)
{
    PMI_SYSTEM System = MiSessionTestSystem;
    const MI_ARCH_DESCRIPTOR *Arch = System->Arch;
    ULONG64 Span = 1ULL << Arch->Level[Arch->PagingLevels - 1].Shift;
    MI_ADDRESS_SPACE Session, Process;

    CHECK(Arch->SessionTopLevelEntries != 0);
    CHECK(NT_SUCCESS(MiSystemReserveSessionRange(System)));
    CHECK(System->SessionEnd > System->SessionStart);
    if (System->SessionEnd <= System->SessionStart)
        return;
    CHECK((System->SessionStart & (Span - 1)) == 0);
    CHECK(System->SessionEnd - System->SessionStart + 1 == Span * Arch->SessionTopLevelEntries);
    CHECK(System->SessionStart >= Arch->SystemAddressStart && System->SessionEnd <= Arch->SystemAddressEnd);
    MmSessionBase = (PVOID)(ULONG_PTR)System->SessionStart;
    MmSessionSize = (SIZE_T)(System->SessionEnd - System->SessionStart + 1);

    CHECK(NT_SUCCESS(MiSessionSpaceCreate(System, &Session)));
    CHECK(NT_SUCCESS(MiAddressSpaceCreate(System, &Process)));
    MiSessionSpaceLink(&Session, &Process, TRUE);
    MiSessionSpaceLink(&Session, &Process, FALSE);
    MiAddressSpaceDestroy(&Process);
    MiSessionSpaceDestroy(&Session);
}

static void
SessionSpaceRelease(PMI_SYSTEM System)
{
    PMI_VAD_ROOT Tree = &System->SystemSpace.VadRoot;
    PMI_VAD_NODE Node = MiVadFind(Tree, System->SessionStart >> PAGE_SHIFT);
    PMI_VAD Vad;

    CHECK(Node != NULL);
    if (Node == NULL)
        return;
    Vad = CONTAINING_RECORD(Node, MI_VAD, Node);
    CHECK(Vad->Type == MiVadSystem);
    CHECK(Node->StartingVpn == System->SessionStart >> PAGE_SHIFT);
    CHECK(Node->EndingVpn == System->SessionEnd >> PAGE_SHIFT);
    MiVadRemove(Tree, Node);
    MI_FREE(Vad);
    System->SessionStart = 0;
    System->SessionEnd = 0;
}

static ULONG
SessionEnumerate(ULONG *Ids, ULONG Capacity)
{
    PEPROCESS Outside = MiSessionTestCurrent;
    KAPC_STATE State;
    PVOID Session;
    ULONG Count = 0;

    for (Session = MmGetNextSession(NULL); Session != NULL; Session = MmGetNextSession(Session))
    {
        CHECK(MmAttachSession(Session, &State) == STATUS_SUCCESS);
        if (Count < Capacity)
            Ids[Count] = MmGetSessionId(MiSessionTestCurrent);
        Count++;
        MmDetachSession(Session, &State);
        CHECK(MiSessionTestCurrent == Outside);
    }
    return Count;
}

static void
SessionNext(void)
{
    struct _EPROCESS_HOST Leader, Keeper0, Keeper1, Outsider;
    MI_PROCESS Native[4];
    ULONG Id0, Id1, Ids[4];
    PVOID Held;

    SessionProcessInitialize(&Leader, &Native[0]);
    SessionProcessInitialize(&Keeper0, &Native[1]);
    SessionProcessInitialize(&Keeper1, &Native[2]);
    SessionProcessInitialize(&Outsider, &Native[3]);
    MiSessionTestCurrent = &Outsider;
    CHECK(MmGetNextSession(NULL) == NULL);

    MiSessionTestCurrent = &Leader;
    CHECK(MmSessionCreate(&Id0) == STATUS_SUCCESS);
    MiSessionAddProcess(&Keeper0);
    CHECK(MmSessionDelete(Id0) == STATUS_SUCCESS);
    CHECK(MmSessionCreate(&Id1) == STATUS_SUCCESS && Id1 != Id0);
    MiSessionAddProcess(&Keeper1);
    CHECK(MmSessionDelete(Id1) == STATUS_SUCCESS);

    MiSessionTestCurrent = &Outsider;
    CHECK(SessionEnumerate(Ids, 4) == 2);
    CHECK((Ids[0] == Id0 && Ids[1] == Id1) || (Ids[0] == Id1 && Ids[1] == Id0));
    Held = MmGetNextSession(NULL);
    CHECK(Held != NULL);
    MmQuitNextSession(Held);

    Held = MmGetSessionById(Id0);
    CHECK(Held != NULL);
    MiSessionRemoveProcess(&Keeper0);
    CHECK(MmGetSessionById(Id0) == NULL);
    CHECK(SessionEnumerate(Ids, 4) == 1 && Ids[0] == Id1);
    MmQuitNextSession(Held);
    CHECK(SessionEnumerate(Ids, 4) == 1 && Ids[0] == Id1);

    MiSessionRemoveProcess(&Keeper1);
    CHECK(MmGetNextSession(NULL) == NULL);
    MiSessionRemoveProcess(&Leader);
    CHECK(MiSessionTestAllocations == 0);
    MiSessionTestCurrent = NULL;
}

void
TestSession(void)
{
    TEST_WORLD World;

    WorldCreate(&World, 256, 1, 1024);
    MiSessionTestSystem = &World.System;
    SessionSpaceRange();
    SessionSetBeforeAnySession();
    SessionMultipleMembers();
    SessionCreateRace();
    SessionLifecycle();
    SessionAttachRundown();
    SessionSetKeepsProcess();
    SessionMoveProcess();
    SessionDriverUnload();
    SessionMoveLastProcess();
    SessionNext();
    SessionSpaceRelease(&World.System);
    MiSessionTestSystem = NULL;
    WorldDestroy(&World);
}
