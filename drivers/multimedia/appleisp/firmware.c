/*
 * PROJECT:     LiberNT Apple ISP Camera Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ISP power, address space, coprocessor boot and IPC channels
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "appleisp.h"

#define ISP_DART_FRAMES ((ULONG)(APPLE_DART_PAGE_SIZE / PAGE_SIZE))
#define ISP_POLL_MS             10

static
VOID
IspConnectInterrupt(
    _Inout_ PISP_DEVICE Isp);

static
VOID
IspDisconnectInterrupt(
    _Inout_ PISP_DEVICE Isp);

static const ULONG IspCoprocWindowOffset[ISP_COPROC_WINDOWS] =
{
    ISP_COPROC_WINDOW_LOW,
    ISP_COPROC_WINDOW_DEBUG,
    ISP_COPROC_WINDOW_CONTROL,
    ISP_COPROC_WINDOW_VERSION,
};

static
volatile ULONG *
IspCoprocRegister(
    _In_ PISP_DEVICE Isp,
    _In_ ULONG Offset)
{
    ULONG Window = Offset & ~(ISP_COPROC_WINDOW_SIZE - 1);
    ULONG Index;

    ASSERT((Offset & 3) == 0);
    for (Index = 0; Index < ISP_COPROC_WINDOWS; ++Index)
    {
        if (IspCoprocWindowOffset[Index] == Window)
        {
            ASSERT(Isp->CoprocWindow[Index] != NULL);
            return (volatile ULONG *)(Isp->CoprocWindow[Index] + (Offset - Window));
        }
    }
    ASSERT(FALSE);
    return NULL;
}

static
ULONG
IspCoprocRead(
    _In_ PISP_DEVICE Isp,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG(IspCoprocRegister(Isp, Offset));
}

static
VOID
IspCoprocWrite(
    _In_ PISP_DEVICE Isp,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG(IspCoprocRegister(Isp, Offset), Value);
}

static
ULONG
IspGpioRead(
    _In_ PISP_DEVICE Isp,
    _In_ ULONG Offset)
{
    ASSERT(Isp->Gpio && Offset + sizeof(ULONG) <= Isp->GpioSize);
    return READ_REGISTER_ULONG((volatile ULONG *)(Isp->Gpio + Offset));
}

static
VOID
IspGpioWrite(
    _In_ PISP_DEVICE Isp,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    ASSERT(Isp->Gpio && Offset + sizeof(ULONG) <= Isp->GpioSize);
    WRITE_REGISTER_ULONG((volatile ULONG *)(Isp->Gpio + Offset), Value);
}

static
VOID
IspMailboxIrqEnable(
    _In_ PISP_DEVICE Isp,
    _In_ ULONG Value)
{
    ASSERT(Isp->Mailbox && Isp->Hardware->MailboxIrqEnable + sizeof(ULONG) <= Isp->MailboxSize);
    WRITE_REGISTER_ULONG((volatile ULONG *)(Isp->Mailbox + Isp->Hardware->MailboxIrqEnable), Value);
}

static
VOID
IspDoorbell(
    _In_ PISP_DEVICE Isp,
    _In_ ULONG Bits)
{
    ASSERT(Isp->Mailbox2 && ISP_MBOX2_IRQ_DOORBELL + sizeof(ULONG) <= Isp->Mailbox2Size);
    WRITE_REGISTER_ULONG((volatile ULONG *)(Isp->Mailbox2 + ISP_MBOX2_IRQ_DOORBELL), Bits);
}

static
VOID
IspSleepMs(
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Interval;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    Interval.QuadPart = -(LONGLONG)Milliseconds * 10000;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

static
ULONG
IspElapsedMs(
    _In_ ULONGLONG Start)
{
    return (ULONG)((KeQueryInterruptTime() - Start) / 10000);
}

static
BOOLEAN
IspWaitGpio(
    _In_ PISP_DEVICE Isp,
    _In_ ULONG Offset,
    _In_ ULONG Wanted,
    _Out_ PULONG Value,
    _Out_ PULONG ElapsedMs)
{
    ULONGLONG Start = KeQueryInterruptTime();

    for (;;)
    {
        *Value = IspGpioRead(Isp, Offset);
        *ElapsedMs = IspElapsedMs(Start);
        if (*Value == Wanted)
            return TRUE;
        if (*ElapsedMs >= ISP_STAGE_TIMEOUT_MS)
            return FALSE;
        IspSleepMs(1);
    }
}

PISP_SURFACE
IspAllocateSurface(
    _In_ PISP_DEVICE Isp,
    _In_ SIZE_T Size,
    _In_ ULONG Type)
{
    PHYSICAL_ADDRESS Low, High, Skip;
    PISP_SURFACE Surface;
    PPFN_NUMBER Pages;
    ULONG64 Span, Chunks, Chunk, Run, Mapped;
    ULONG Index, Frame;
    NTSTATUS Status = STATUS_SUCCESS;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (!Size || Size > ISP_MAX_SURFACE || !Isp->DartReady || !Isp->IovaBits)
        return NULL;
    Span = ((ULONG64)Size + APPLE_DART_PAGE_SIZE - 1) & ~(APPLE_DART_PAGE_SIZE - 1);
    Chunks = Span / APPLE_DART_PAGE_SIZE;
    Surface = ExAllocatePoolZero(NonPagedPool, sizeof(*Surface), ISP_TAG);
    if (!Surface)
        return NULL;
    InitializeListHead(&Surface->Link);
    Surface->Size = (SIZE_T)Span;
    Surface->Type = Type;
    Low.QuadPart = 0;
    High.QuadPart = -1;
    Skip.QuadPart = (LONGLONG)APPLE_DART_PAGE_SIZE;
    Surface->Mdl = MmAllocatePagesForMdlEx(Low, High, Skip, (SIZE_T)Span, MmWriteCombined,
                                           MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS | MM_ALLOCATE_FULLY_REQUIRED);
    if (!Surface->Mdl || MmGetMdlByteCount(Surface->Mdl) != Span)
    {
        DbgPrint("appleisp: no memory for a %I64x-byte buffer\n", Span);
        goto Fail;
    }
    Pages = MmGetMdlPfnArray(Surface->Mdl);
    for (Chunk = 0; Chunk < Chunks; ++Chunk)
    {
        if (Pages[Chunk * ISP_DART_FRAMES] % ISP_DART_FRAMES)
            goto Misaligned;
        for (Frame = 1; Frame < ISP_DART_FRAMES; ++Frame)
        {
            if (Pages[Chunk * ISP_DART_FRAMES + Frame] != Pages[Chunk * ISP_DART_FRAMES] + Frame)
                goto Misaligned;
        }
    }
    Surface->Virtual = MmMapLockedPagesSpecifyCache(Surface->Mdl, KernelMode, MmWriteCombined, NULL, FALSE,
                                                    NormalPagePriority);
    if (!Surface->Virtual)
        goto Fail;

    KeWaitForSingleObject(&Isp->SurfaceLock, Executive, KernelMode, FALSE, NULL);
    Index = RtlFindClearBitsAndSet(&Isp->IovaMap, (ULONG)Chunks, 0);
    if (Index == MAXULONG)
    {
        KeReleaseMutex(&Isp->SurfaceLock, FALSE);
        DbgPrint("appleisp: no device addresses left for a %I64x-byte buffer\n", Span);
        goto Fail;
    }
    Surface->Iova = Isp->IovaBase + (ULONG64)Index * APPLE_DART_PAGE_SIZE;
    for (Chunk = 0, Mapped = 0; NT_SUCCESS(Status) && Chunk < Chunks; Chunk += Run)
    {
        for (Run = 1; Chunk + Run < Chunks &&
                      Pages[(Chunk + Run) * ISP_DART_FRAMES] == Pages[Chunk * ISP_DART_FRAMES] + Run * ISP_DART_FRAMES;
             ++Run)
            ;
        Status = AppleDartDomainMap(&Isp->Dart, Surface->Iova + Chunk * APPLE_DART_PAGE_SIZE,
                                    (ULONG64)Pages[Chunk * ISP_DART_FRAMES] << PAGE_SHIFT,
                                    (SIZE_T)(Run * APPLE_DART_PAGE_SIZE));
        if (NT_SUCCESS(Status))
            Mapped = Chunk + Run;
    }
    if (!NT_SUCCESS(Status))
    {
        if (Mapped)
            AppleDartDomainUnmap(&Isp->Dart, Surface->Iova, (SIZE_T)(Mapped * APPLE_DART_PAGE_SIZE));
        RtlClearBits(&Isp->IovaMap, Index, (ULONG)Chunks);
        KeReleaseMutex(&Isp->SurfaceLock, FALSE);
        DbgPrint("appleisp: mapping a %I64x-byte buffer at %I64x failed: %lx\n", Span, Surface->Iova, Status);
        Surface->Iova = 0;
        goto Fail;
    }
    KeReleaseMutex(&Isp->SurfaceLock, FALSE);
    return Surface;

Misaligned:
    DbgPrint("appleisp: buffer pages are not in 16 KB physical blocks\n");
Fail:
    if (Surface->Virtual)
        MmUnmapLockedPages(Surface->Virtual, Surface->Mdl);
    if (Surface->Mdl)
    {
        MmFreePagesFromMdl(Surface->Mdl);
        ExFreePool(Surface->Mdl);
    }
    ExFreePoolWithTag(Surface, ISP_TAG);
    return NULL;
}

VOID
IspFreeSurface(
    _In_ PISP_DEVICE Isp,
    _In_opt_ PISP_SURFACE Surface)
{
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (!Surface)
        return;
    ASSERT(IsListEmpty(&Surface->Link));
    ASSERT((Surface->Size % APPLE_DART_PAGE_SIZE) == 0);
    KeWaitForSingleObject(&Isp->SurfaceLock, Executive, KernelMode, FALSE, NULL);
    if (Isp->DartReady)
        AppleDartDomainUnmap(&Isp->Dart, Surface->Iova, Surface->Size);
    if (Isp->IovaBits)
    {
        ASSERT(Surface->Iova >= Isp->IovaBase &&
               (Surface->Iova - Isp->IovaBase) / APPLE_DART_PAGE_SIZE < Isp->IovaPages);
        RtlClearBits(&Isp->IovaMap, (ULONG)((Surface->Iova - Isp->IovaBase) / APPLE_DART_PAGE_SIZE),
                     (ULONG)(Surface->Size / APPLE_DART_PAGE_SIZE));
    }
    KeReleaseMutex(&Isp->SurfaceLock, FALSE);
    MmUnmapLockedPages(Surface->Virtual, Surface->Mdl);
    MmFreePagesFromMdl(Surface->Mdl);
    ExFreePool(Surface->Mdl);
    ExFreePoolWithTag(Surface, ISP_TAG);
}

PVOID
IspTranslate(
    _In_opt_ PISP_SURFACE Surface,
    _In_ ULONG64 Iova,
    _In_ ULONG64 Length)
{
    if (!Surface || Iova < Surface->Iova || Length > Surface->Size || Iova - Surface->Iova > Surface->Size - Length)
        return NULL;
    return Surface->Virtual + (Iova - Surface->Iova);
}

static
VOID
IspReadMessage(
    _In_ PISP_CHANNEL Channel,
    _In_ ULONG Index,
    _Out_ PISP_MESSAGE Message)
{
    volatile ISP_MESSAGE *Slot;
    ULONG Argument;

    ASSERT(Channel->Ring && Index < Channel->Count);
    Slot = &Channel->Ring[Index];
    KeMemoryBarrier();
    for (Argument = 0; Argument < RTL_NUMBER_OF(Message->Argument); ++Argument)
        Message->Argument[Argument] = Slot->Argument[Argument];
}

static
VOID
IspWriteMessage(
    _In_ PISP_CHANNEL Channel,
    _In_ ULONG Index,
    _In_ const ISP_MESSAGE *Message)
{
    volatile ISP_MESSAGE *Slot;
    ULONG Argument;

    ASSERT(Channel->Ring && Index < Channel->Count);
    Slot = &Channel->Ring[Index];
    for (Argument = 1; Argument < RTL_NUMBER_OF(Message->Argument); ++Argument)
        Slot->Argument[Argument] = Message->Argument[Argument];
    KeMemoryBarrier();
    Slot->Argument[0] = Message->Argument[0];
    KeMemoryBarrier();
}

static
NTSTATUS
IspHandleTerminal(
    _In_ PISP_DEVICE Isp,
    _In_ PISP_CHANNEL Channel)
{
    ULONG64 Iova = Channel->Request.Argument[0] & ~(ULONG64)ISP_IPC_FLAG_TERMINAL_ACK;
    ULONG64 Size = Channel->Request.Argument[1];
    CHAR Line[257];
    const CHAR *Text;
    ULONG Length;

    if (Iova && Size && Isp->LogSurface)
    {
        Length = (ULONG)min(Size, sizeof(Line) - 1);
        Text = IspTranslate(Isp->LogSurface, Iova, Length);
        if (Text)
        {
            RtlCopyMemory(Line, Text, Length);
            Line[Length] = ANSI_NULL;
            while (Length && (Line[Length - 1] == '\n' || Line[Length - 1] == '\r' || !Line[Length - 1]))
                Line[--Length] = ANSI_NULL;
            if (Length)
                DbgPrint("appleisp: firmware says: %s\n", Line);
        }
    }
    RtlZeroMemory(&Channel->Reply, sizeof(Channel->Reply));
    Channel->Reply.Argument[0] = ISP_IPC_FLAG_ACK;
    return STATUS_SUCCESS;
}

static
NTSTATUS
IspHandleSharedMalloc(
    _In_ PISP_DEVICE Isp,
    _In_ PISP_CHANNEL Channel)
{
    PISP_MESSAGE Request = &Channel->Request;
    PISP_SURFACE Surface;
    ULONG Type;

    RtlZeroMemory(&Channel->Reply, sizeof(Channel->Reply));
    if (Request->Argument[0] != 0)
    {
        Channel->Reply.Argument[0] = Request->Argument[0] | ISP_IPC_FLAG_ACK;
        return STATUS_SUCCESS;
    }
    if (!Request->Argument[1] || Request->Argument[1] > ISP_MAX_SURFACE)
        return STATUS_INVALID_PARAMETER;
    Type = (ULONG)Request->Argument[2];
    Surface = IspAllocateSurface(Isp, (SIZE_T)Request->Argument[1], Type);
    if (!Surface)
        return STATUS_INSUFFICIENT_RESOURCES;
    InsertTailList(&Isp->FirmwareSurfaces, &Surface->Link);
    if (Type == ISP_SURFACE_TYPE_LOG)
        Isp->LogSurface = Surface;
    else if (Type == ISP_SURFACE_TYPE_MISC && Surface->Size == ISP_SURFACE_BUFFER_LIST_SIZE)
        Isp->BufferListSurface = Surface;
    Channel->Reply.Argument[0] = Surface->Iova | ISP_IPC_FLAG_ACK;
    DbgPrint("appleisp: firmware buffer of %I64x bytes, type %08lx, at %I64x\n", Request->Argument[1], Type,
             Surface->Iova);
    return STATUS_SUCCESS;
}

static
VOID
IspServiceChannel(
    _In_ PISP_DEVICE Isp,
    _In_ PISP_CHANNEL Channel,
    _In_ NTSTATUS (*Handler)(PISP_DEVICE, PISP_CHANNEL))
{
    ULONG Handled, Flag;
    NTSTATUS Status;

    for (Handled = 0; Handled < Channel->Count; ++Handled)
    {
        IspReadMessage(Channel, Channel->Cursor, &Channel->Request);
        Flag = (ULONG)(Channel->Request.Argument[0] & 0xf);
        if (Flag == ISP_IPC_FLAG_ACK || Flag == ISP_IPC_FLAG_TERMINAL_ACK)
            return;
        Status = Handler(Isp, Channel);
        if (!NT_SUCCESS(Status))
        {
            if (Channel->Failed != Channel->Request.Argument[0] + Channel->Request.Argument[1])
            {
                DbgPrint("appleisp: %s request %I64x %I64x %I64x not handled: %lx\n", Channel->Name,
                         Channel->Request.Argument[0], Channel->Request.Argument[1], Channel->Request.Argument[2],
                         Status);
            }
            Channel->Failed = Channel->Request.Argument[0] + Channel->Request.Argument[1];
            return;
        }
        IspWriteMessage(Channel, Channel->Cursor, &Channel->Reply);
        IspDoorbell(Isp, Channel->Doorbell);
        Channel->Cursor = (Channel->Cursor + 1) % Channel->Count;
    }
}

static
VOID
IspServiceChannels(
    _In_ PISP_DEVICE Isp)
{
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    KeWaitForSingleObject(&Isp->ChannelLock, Executive, KernelMode, FALSE, NULL);
    if (Isp->ChannelsReady)
    {
        IspServiceChannel(Isp, Isp->SharedMalloc, IspHandleSharedMalloc);
        IspServiceChannel(Isp, Isp->Terminal, IspHandleTerminal);
        IspServiceChannel(Isp, Isp->BufferToHost, IspHandleBufferReturn);
    }
    KeReleaseMutex(&Isp->ChannelLock, FALSE);
}

NTSTATUS
IspSend(
    _In_ PISP_DEVICE Isp,
    _In_ PISP_CHANNEL Channel,
    _In_ ULONG TimeoutMs)
{
    LARGE_INTEGER Interval;
    ISP_MESSAGE Reply;
    ULONGLONG Start;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(KeReadStateMutex(&Isp->CommandLock) != 1);
    ASSERT(Channel && Channel->Type == ISP_IPC_CHAN_COMMAND);
    if (!Isp->ChannelsReady)
        return STATUS_DEVICE_NOT_READY;
    IspWriteMessage(Channel, Channel->Cursor, &Channel->Request);
    IspDoorbell(Isp, Channel->Doorbell);
    if (!TimeoutMs)
        return STATUS_SUCCESS;
    Start = KeQueryInterruptTime();
    Interval.QuadPart = -10000;
    for (;;)
    {
        KeClearEvent(&Isp->ReplyEvent);
        IspServiceChannels(Isp);
        IspReadMessage(Channel, Channel->Cursor, &Reply);
        if (Reply.Argument[0] == (Channel->Request.Argument[0] | ISP_IPC_FLAG_ACK))
        {
            Channel->Reply = Reply;
            Channel->Cursor = (Channel->Cursor + 1) % Channel->Count;
            return STATUS_SUCCESS;
        }
        if (IspElapsedMs(Start) >= TimeoutMs)
        {
            DbgPrint("appleisp: %s request %I64x %I64x %I64x got no answer in %lu ms\n", Channel->Name,
                     Channel->Request.Argument[0], Channel->Request.Argument[1], Channel->Request.Argument[2], TimeoutMs);
            return STATUS_IO_TIMEOUT;
        }
        KeWaitForSingleObject(&Isp->ReplyEvent, Executive, KernelMode, FALSE, &Interval);
    }
}

static
NTSTATUS
IspBootStage1(
    _In_ PISP_DEVICE Isp)
{
    ULONG Index, Polls, Status, Value, Elapsed;
    ULONGLONG Start;
    NTSTATUS Result;

    Result = ApplePmgrPowerOnDevice(&Isp->Tree, Isp->Tree.Node);
    if (!NT_SUCCESS(Result))
    {
        DbgPrint("appleisp: firmware stage 1: the ISP power domains did not turn on: %lx\n", Result);
        return Result;
    }
    Isp->CorePowered = TRUE;
    IspGpioWrite(Isp, ISP_GPIO_CLOCK_EN, 1);
    IspCoprocWrite(Isp, ISP_COPROC_EDPRCR, ISP_COPROC_EDPRCR_VALUE);
    IspCoprocWrite(Isp, ISP_COPROC_FABRIC_0, ISP_COPROC_FABRIC_VALUE);
    IspCoprocWrite(Isp, ISP_COPROC_FABRIC_1, ISP_COPROC_FABRIC_VALUE);
    IspCoprocWrite(Isp, ISP_COPROC_FABRIC_2, ISP_COPROC_FABRIC_VALUE);
    IspCoprocWrite(Isp, ISP_COPROC_FABRIC_3, ISP_COPROC_FABRIC_VALUE);
    for (Index = 0; Index < ISP_COPROC_IRQ_MASK_COUNT; ++Index)
        IspCoprocWrite(Isp, ISP_COPROC_IRQ_MASK_0 + Index * sizeof(ULONG), MAXULONG);
    for (Polls = 0; Polls < ISP_RESET_POLLS && IspCoprocRead(Isp, ISP_COPROC_BUSY_0); ++Polls)
        ;
    for (Polls = 0; Polls < ISP_RESET_POLLS && IspCoprocRead(Isp, ISP_COPROC_BUSY_1); ++Polls)
        ;
    Start = KeQueryInterruptTime();
    while (!((Status = IspCoprocRead(Isp, ISP_COPROC_STATUS)) & ISP_COPROC_STATUS_WFI))
    {
        if (IspElapsedMs(Start) >= ISP_STAGE_TIMEOUT_MS)
        {
            DbgPrint("appleisp: firmware stage 1: the coprocessor never idled (status %08lx)\n", Status);
            return STATUS_DEVICE_NOT_READY;
        }
        IspSleepMs(1);
    }
    for (Index = 0; Index < ISP_GPIO_COUNT; ++Index)
        IspGpioWrite(Isp, Index * sizeof(ULONG), 0);
    IspMailboxIrqEnable(Isp, 0);
    IspCoprocWrite(Isp, ISP_COPROC_CONTROL, 0);
    IspCoprocWrite(Isp, ISP_COPROC_CONTROL, ISP_COPROC_CONTROL_RUN);
    if (!IspWaitGpio(Isp, ISP_GPIO_7, ISP_MAGIC_READY, &Value, &Elapsed))
    {
        DbgPrint("appleisp: firmware stage 1: the coprocessor started but never reported ready (%08lx)\n", Value);
        return STATUS_IO_TIMEOUT;
    }
    DbgPrint("appleisp: firmware stage 1: coprocessor running after %lu ms (idle status %08lx)\n", Elapsed, Status);
    return STATUS_SUCCESS;
}

static
NTSTATUS
IspBootStage2(
    _In_ PISP_DEVICE Isp)
{
    ISP_BOOT_ARGUMENTS Arguments;
    ULONG Channels, ArgumentOffset, ExtraSize, CommandOffset, Value, Elapsed;
    ULONG64 ArgumentIova, SharedBase;

    Channels = IspGpioRead(Isp, ISP_GPIO_0);
    ArgumentOffset = IspGpioRead(Isp, ISP_GPIO_1);
    ExtraSize = IspGpioRead(Isp, ISP_GPIO_3);
    CommandOffset = ArgumentOffset + ISP_IPC_ARGS_GAP + sizeof(ISP_BOOT_ARGUMENTS) + ISP_IPC_ARGS_GAP;
    if (!Channels || Channels > ISP_MAX_IPC_CHANNELS || ArgumentOffset > Isp->Ipc->Size ||
        CommandOffset < ArgumentOffset || CommandOffset > Isp->Ipc->Size - ISP_COMMAND_SPACE ||
        ExtraSize > ISP_MAX_SURFACE)
    {
        DbgPrint("appleisp: firmware stage 2: unusable layout: %lu channels, arguments at %lx, extra heap %lx\n",
                 Channels, ArgumentOffset, ExtraSize);
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }
    if (Channels != 7)
        DbgPrint("appleisp: firmware stage 2: %lu channels where 7 were seen so far\n", Channels);
    Isp->ChannelCount = Channels;
    if (ExtraSize)
    {
        Isp->Extra = IspAllocateSurface(Isp, ExtraSize, 0);
        if (!Isp->Extra)
            return STATUS_INSUFFICIENT_RESOURCES;
    }
    ArgumentIova = Isp->Ipc->Iova + ArgumentOffset + ISP_IPC_ARGS_GAP;
    Isp->CommandIova = Isp->Ipc->Iova + CommandOffset;
    Isp->Command = Isp->Ipc->Virtual + CommandOffset;
    Isp->CommandSpace = (ULONG)(Isp->Ipc->Size - CommandOffset);
    SharedBase = Isp->HeapTop & MAXULONG;
    ASSERT(SharedBase < ISP_SHARED_WINDOW_END);

    RtlZeroMemory(&Arguments, sizeof(Arguments));
    Arguments.IpcIova = Isp->Ipc->Iova;
    Arguments.IpcSize = (ULONG)Isp->Ipc->Size;
    Arguments.SharedBase = SharedBase;
    Arguments.SharedSize = ISP_SHARED_WINDOW_END - SharedBase;
    Arguments.ExtraIova = Isp->Extra ? Isp->Extra->Iova : 0;
    Arguments.ExtraSize = Isp->Extra ? Isp->Extra->Size : 0;
    Arguments.PlatformId = Isp->PlatformId;
    Arguments.Value68 = ISP_BOOT_VALUE_68;
    Arguments.ValueA4 = ISP_BOOT_VALUE_A4;
    Arguments.ValueBC = (ULONG)(ArgumentIova + sizeof(Arguments) - ISP_BOOT_VALUE_BC_BACK);
    Arguments.Value17C = ISP_BOOT_VALUE_17C;
    RtlCopyMemory(Isp->Ipc->Virtual + ArgumentOffset + ISP_IPC_ARGS_GAP, &Arguments, sizeof(Arguments));
    KeMemoryBarrier();
    IspGpioWrite(Isp, ISP_GPIO_0, (ULONG)ArgumentIova);
    IspGpioWrite(Isp, ISP_GPIO_1, (ULONG)(ArgumentIova >> 32));
    KeMemoryBarrier();
    IspGpioWrite(Isp, ISP_GPIO_7, ISP_MAGIC_ARGS);
    if (!IspWaitGpio(Isp, ISP_GPIO_7, ISP_MAGIC_READY, &Value, &Elapsed))
    {
        DbgPrint("appleisp: firmware stage 2: the boot arguments at %I64x were never taken (%08lx)\n", ArgumentIova,
                 Value);
        return STATUS_IO_TIMEOUT;
    }
    DbgPrint("appleisp: firmware stage 2: boot arguments taken after %lu ms: %lu channels, extra heap %lx at %I64x\n",
             Elapsed, Channels, ExtraSize, Arguments.ExtraIova);
    return STATUS_SUCCESS;
}

static
PISP_CHANNEL
IspFindChannel(
    _In_ PISP_DEVICE Isp,
    _In_z_ PCSTR Name)
{
    ULONG Index;

    for (Index = 0; Index < Isp->ChannelCount; ++Index)
    {
        if (!_stricmp(Isp->Channels[Index].Name, Name))
            return &Isp->Channels[Index];
    }
    return NULL;
}

static
NTSTATUS
IspBootStage3(
    _In_ PISP_DEVICE Isp)
{
    ISP_CHANNEL_DESCRIPTOR Descriptor;
    PISP_CHANNEL_DESCRIPTOR Table;
    PISP_CHANNEL Channel;
    ISP_MESSAGE Empty;
    ULONG64 TableIova;
    ULONG Index, Slot, Value, Elapsed;

    TableIova = IspGpioRead(Isp, ISP_GPIO_0) | ((ULONG64)IspGpioRead(Isp, ISP_GPIO_1) << 32);
    Table = IspTranslate(Isp->Ipc, TableIova, (ULONG64)Isp->ChannelCount * sizeof(*Table));
    if (!Table)
    {
        DbgPrint("appleisp: firmware stage 3: channel table at %I64x is outside the IPC buffer\n", TableIova);
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }
    for (Index = 0; Index < Isp->ChannelCount; ++Index)
    {
        Channel = &Isp->Channels[Index];
        RtlZeroMemory(Channel, sizeof(*Channel));
        RtlCopyMemory(&Descriptor, &Table[Index], sizeof(Descriptor));
        RtlCopyMemory(Channel->Name, Descriptor.Name, sizeof(Channel->Name) - 1);
        Channel->Type = Descriptor.Type;
        Channel->Source = Descriptor.Source;
        Channel->Count = Descriptor.Count;
        Channel->Iova = Descriptor.Iova;
        if (Channel->Source >= 32 || !Channel->Count || Channel->Type > ISP_IPC_CHAN_REPORT)
        {
            DbgPrint("appleisp: firmware stage 3: channel %lu '%s' is malformed (type %lu, source %lu, %lu slots)\n",
                     Index, Channel->Name, Channel->Type, Channel->Source, Channel->Count);
            return STATUS_DEVICE_PROTOCOL_ERROR;
        }
        Channel->Doorbell = 1UL << Channel->Source;
        Channel->Ring = IspTranslate(Isp->Ipc, Channel->Iova, (ULONG64)Channel->Count * sizeof(ISP_MESSAGE));
        if (!Channel->Ring || ((ULONG_PTR)Channel->Ring & (sizeof(ULONG64) - 1)))
        {
            DbgPrint("appleisp: firmware stage 3: channel '%s' ring at %I64x is outside the IPC buffer\n",
                     Channel->Name, Channel->Iova);
            return STATUS_DEVICE_PROTOCOL_ERROR;
        }
        DbgPrint("appleisp: channel %s: type %lu, %lu slots, doorbell %lu, at %I64x\n", Channel->Name, Channel->Type,
                 Channel->Count, Channel->Source, Channel->Iova);
    }
    Isp->Terminal = IspFindChannel(Isp, "TERMINAL");
    Isp->Io = IspFindChannel(Isp, "IO");
    Isp->Debug = IspFindChannel(Isp, "DEBUG");
    Isp->BufferToFirmware = IspFindChannel(Isp, "BUF_H2T");
    Isp->BufferToHost = IspFindChannel(Isp, "BUF_T2H");
    Isp->SharedMalloc = IspFindChannel(Isp, "SHAREDMALLOC");
    Isp->IoToHost = IspFindChannel(Isp, "IO_T2H");
    if (!Isp->Terminal || !Isp->Io || !Isp->Debug || !Isp->BufferToFirmware || !Isp->BufferToHost ||
        !Isp->SharedMalloc || !Isp->IoToHost || Isp->Io->Type != ISP_IPC_CHAN_COMMAND ||
        Isp->BufferToFirmware->Type != ISP_IPC_CHAN_COMMAND)
    {
        DbgPrint("appleisp: firmware stage 3: a required channel is missing\n");
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }
    RtlZeroMemory(&Empty, sizeof(Empty));
    Empty.Argument[0] = ISP_IPC_FLAG_ACK;
    for (Index = 0; Index < Isp->ChannelCount; ++Index)
    {
        Channel = &Isp->Channels[Index];
        if (Channel->Type != ISP_IPC_CHAN_COMMAND)
            continue;
        for (Slot = 0; Slot < Channel->Count; ++Slot)
            IspWriteMessage(Channel, Slot, &Empty);
    }
    KeMemoryBarrier();
    IspGpioWrite(Isp, ISP_GPIO_3, ISP_MAGIC_READY);
    if (!IspWaitGpio(Isp, ISP_GPIO_3, 0, &Value, &Elapsed))
    {
        DbgPrint("appleisp: firmware stage 3: the channels were never accepted (%08lx)\n", Value);
        return STATUS_IO_TIMEOUT;
    }
    KeWaitForSingleObject(&Isp->ChannelLock, Executive, KernelMode, FALSE, NULL);
    Isp->ChannelsReady = TRUE;
    KeReleaseMutex(&Isp->ChannelLock, FALSE);
    DbgPrint("appleisp: firmware stage 3: %lu IPC channels accepted after %lu ms\n", Isp->ChannelCount, Elapsed);
    return STATUS_SUCCESS;
}

static
VOID
IspFirmwareTeardown(
    _In_ PISP_DEVICE Isp)
{
    PLIST_ENTRY Entry;
    PISP_SURFACE Surface;
    NTSTATUS Status;

    ASSERT(KeReadStateMutex(&Isp->CommandLock) != 1);
    if (Isp->IrqEnabled)
    {
        IspMailboxIrqEnable(Isp, 0);
        IspGpioWrite(Isp, ISP_GPIO_1, ISP_MAGIC_IRQ_OFF);
        Isp->IrqEnabled = FALSE;
    }
    IspDisconnectInterrupt(Isp);
    KeWaitForSingleObject(&Isp->ChannelLock, Executive, KernelMode, FALSE, NULL);
    Isp->ChannelsReady = FALSE;
    Isp->ChannelCount = 0;
    Isp->Terminal = Isp->Io = Isp->Debug = NULL;
    Isp->BufferToFirmware = Isp->BufferToHost = Isp->SharedMalloc = Isp->IoToHost = NULL;
    Isp->Command = NULL;
    Isp->CommandIova = 0;
    Isp->CommandSpace = 0;
    KeReleaseMutex(&Isp->ChannelLock, FALSE);
    if (Isp->CorePowered)
        IspCoprocWrite(Isp, ISP_COPROC_CONTROL, 0);
    Status = ApplePmgrPowerOffDomains(&Isp->Tree, Isp->Tree.Node, 1);
    Isp->CorePowered = FALSE;
    if (!NT_SUCCESS(Status))
        DbgPrint("appleisp: the ISP core power domains did not all turn off: %lx\n", Status);
    IspFreeSurface(Isp, Isp->Extra);
    Isp->Extra = NULL;
    Isp->LogSurface = NULL;
    Isp->BufferListSurface = NULL;
    while (!IsListEmpty(&Isp->FirmwareSurfaces))
    {
        Entry = RemoveHeadList(&Isp->FirmwareSurfaces);
        Surface = CONTAINING_RECORD(Entry, ISP_SURFACE, Link);
        InitializeListHead(&Surface->Link);
        IspFreeSurface(Isp, Surface);
    }
    InterlockedExchange(&Isp->FirmwareState, IspFirmwareOff);
}

NTSTATUS
IspFirmwareBoot(
    _In_ PISP_DEVICE Isp)
{
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(Isp->HardwareStarted && Isp->Ipc);
    KeWaitForSingleObject(&Isp->CommandLock, Executive, KernelMode, FALSE, NULL);
    if (Isp->FirmwareState != IspFirmwareOff)
    {
        KeReleaseMutex(&Isp->CommandLock, FALSE);
        return Isp->FirmwareState == IspFirmwareRunning ? STATUS_SUCCESS : STATUS_DEVICE_BUSY;
    }
    InterlockedExchange(&Isp->FirmwareState, IspFirmwareStarting);
    ASSERT(!Isp->ChannelsReady && IsListEmpty(&Isp->FirmwareSurfaces) && !Isp->Extra);
    Status = IspBootStage1(Isp);
    if (NT_SUCCESS(Status))
        Status = IspBootStage2(Isp);
    if (NT_SUCCESS(Status))
        Status = IspBootStage3(Isp);
    if (NT_SUCCESS(Status))
    {
        IspConnectInterrupt(Isp);
        IspMailboxIrqEnable(Isp, ISP_MBOX_IRQ_ENABLE_ALL);
        Isp->IrqEnabled = TRUE;
        Status = IspStartCommandProcessor(Isp);
    }
    if (NT_SUCCESS(Status))
    {
        InterlockedExchange(&Isp->FirmwareState, IspFirmwareRunning);
        KeSetEvent(&Isp->WorkEvent, IO_NO_INCREMENT, FALSE);
        DbgPrint("appleisp: firmware running, %ld interrupts so far\n", Isp->Interrupts);
    }
    else
    {
        DbgPrint("appleisp: firmware boot failed: %lx\n", Status);
        IspFirmwareTeardown(Isp);
    }
    KeReleaseMutex(&Isp->CommandLock, FALSE);
    return Status;
}

VOID
IspFirmwareShutdown(
    _In_ PISP_DEVICE Isp)
{
    ISP_CMD_OPCODE Suspend;
    ULONG Value = 0, Elapsed = 0;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    KeWaitForSingleObject(&Isp->CommandLock, Executive, KernelMode, FALSE, NULL);
    if (Isp->FirmwareState == IspFirmwareOff)
    {
        KeReleaseMutex(&Isp->CommandLock, FALSE);
        return;
    }
    ASSERT(Isp->FirmwareState == IspFirmwareRunning);
    InterlockedExchange(&Isp->FirmwareState, IspFirmwareStopping);
    IspGpioWrite(Isp, ISP_GPIO_0, ISP_MAGIC_ARGS);
    Suspend.Opcode = ISP_OPCODE(CISP_CMD_SUSPEND);
    Status = IspCommand(Isp, &Suspend, sizeof(Suspend), 0);
    if (NT_SUCCESS(Status) && !IspWaitGpio(Isp, ISP_GPIO_0, ISP_MAGIC_READY, &Value, &Elapsed))
        Status = STATUS_IO_TIMEOUT;
    DbgPrint("appleisp: firmware suspended after %lu ms: %lx (%08lx)\n", Elapsed, Status, Value);
    IspFirmwareTeardown(Isp);
    KeReleaseMutex(&Isp->CommandLock, FALSE);
    DbgPrint("appleisp: firmware stopped, ISP core powered down\n");
}

static
BOOLEAN
NTAPI
IspInterruptService(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID Context)
{
    PISP_DEVICE Isp = Context;
    ULONG Pending;

    UNREFERENCED_PARAMETER(Interrupt);
    Pending = READ_REGISTER_ULONG((volatile ULONG *)(Isp->Mailbox + ISP_MBOX_IRQ_INTERRUPT));
    WRITE_REGISTER_ULONG((volatile ULONG *)(Isp->Mailbox2 + ISP_MBOX2_IRQ_ACK), Pending);
    if (!Pending)
        return FALSE;
    InterlockedIncrement(&Isp->Interrupts);
    KeInsertQueueDpc(&Isp->Dpc, NULL, NULL);
    return TRUE;
}

static
VOID
NTAPI
IspDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID Context,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    PISP_DEVICE Isp = Context;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Argument1);
    UNREFERENCED_PARAMETER(Argument2);
    ASSERT(Isp && KeGetCurrentIrql() == DISPATCH_LEVEL);
    if (!InterlockedExchange(&Isp->InterruptLogged, 1))
        DbgPrint("appleisp: interrupt fired\n");
    KeSetEvent(&Isp->ReplyEvent, IO_NO_INCREMENT, FALSE);
    KeSetEvent(&Isp->WorkEvent, IO_NO_INCREMENT, FALSE);
}

static
VOID
NTAPI
IspWorker(
    _In_ PVOID Context)
{
    PISP_DEVICE Isp = Context;
    LARGE_INTEGER Interval;
    PLARGE_INTEGER Timeout;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    for (;;)
    {
        Timeout = NULL;
        if (Isp->FirmwareState == IspFirmwareRunning && (!Isp->Interrupt || !Isp->Interrupts))
        {
            Interval.QuadPart = -10000LL * ISP_POLL_MS;
            Timeout = &Interval;
        }
        KeWaitForSingleObject(&Isp->WorkEvent, Executive, KernelMode, FALSE, Timeout);
        if (Isp->StopWorker)
            break;
        if (Isp->FirmwareState != IspFirmwareRunning)
            continue;
        IspServiceChannels(Isp);
        if (Isp->Streaming)
            IspSubmitBuffers(Isp);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
IspConnectInterrupt(
    _Inout_ PISP_DEVICE Isp)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Partial = &Isp->InterruptResource;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (!Isp->HaveInterrupt || Isp->Interrupt)
        return;
    Status = IoConnectInterrupt(&Isp->Interrupt, IspInterruptService, Isp, NULL, Partial->u.Interrupt.Vector,
                                (KIRQL)Partial->u.Interrupt.Level, (KIRQL)Partial->u.Interrupt.Level,
                                (Partial->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ? Latched : LevelSensitive,
                                Partial->ShareDisposition == CmResourceShareShared, Partial->u.Interrupt.Affinity,
                                FALSE);
    if (!NT_SUCCESS(Status))
    {
        Isp->Interrupt = NULL;
        DbgPrint("appleisp: interrupt vector %lu not connected (%lx), polling\n", Partial->u.Interrupt.Vector, Status);
        return;
    }
    DbgPrint("appleisp: interrupt vector %lu level %lu connected\n", Partial->u.Interrupt.Vector,
             Partial->u.Interrupt.Level);
}

static
VOID
IspDisconnectInterrupt(
    _Inout_ PISP_DEVICE Isp)
{
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (!Isp->Interrupt)
        return;
    IoDisconnectInterrupt(Isp->Interrupt);
    Isp->Interrupt = NULL;
    KeFlushQueuedDpcs();
}

static
PUCHAR
IspMapNamed(
    _In_ PISP_DEVICE Isp,
    _In_z_ PCSTR Name,
    _Out_ PULONG Size)
{
    ULONG Index = FdtDriverFindRegIndex(&Isp->Tree, Isp->Tree.Node, Name);
    ULONG64 Address = 0;
    PUCHAR Base;

    *Size = 0;
    if (Index == MAXULONG)
    {
        DbgPrint("appleisp: the ISP node has no '%s' registers\n", Name);
        return NULL;
    }
    Base = FdtDriverMapReg(&Isp->Tree, Isp->Tree.Node, Index, &Address, Size);
    if (!Base)
        DbgPrint("appleisp: the '%s' registers at %I64x could not be mapped\n", Name, Address);
    return Base;
}

static
NTSTATUS
IspMapRegisters(
    _In_ PISP_DEVICE Isp)
{
    PHYSICAL_ADDRESS Physical;
    ULONG64 Base, Size;
    ULONG Index, Window;

    Index = FdtDriverFindRegIndex(&Isp->Tree, Isp->Tree.Node, "coproc");
    if (Index == MAXULONG || !FdtDriverReadReg(&Isp->Tree, Isp->Tree.Node, Index, &Base, &Size))
    {
        DbgPrint("appleisp: the ISP node has no coprocessor registers\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    for (Window = 0; Window < ISP_COPROC_WINDOWS; ++Window)
    {
        if (IspCoprocWindowOffset[Window] + ISP_COPROC_WINDOW_SIZE > Size)
        {
            DbgPrint("appleisp: the coprocessor registers %I64x+%I64x do not reach offset %lx\n", Base, Size,
                     IspCoprocWindowOffset[Window]);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        Physical.QuadPart = (LONGLONG)(Base + IspCoprocWindowOffset[Window]);
        Isp->CoprocWindow[Window] = MmMapIoSpace(Physical, ISP_COPROC_WINDOW_SIZE, MmNonCached);
        if (!Isp->CoprocWindow[Window])
            return STATUS_INSUFFICIENT_RESOURCES;
    }
    Isp->Mailbox = IspMapNamed(Isp, "mbox", &Isp->MailboxSize);
    Isp->Gpio = IspMapNamed(Isp, "gpio", &Isp->GpioSize);
    Isp->Mailbox2 = IspMapNamed(Isp, "mbox2", &Isp->Mailbox2Size);
    if (!Isp->Mailbox || !Isp->Gpio || !Isp->Mailbox2)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    if (Isp->GpioSize < ISP_GPIO_CLOCK_EN + sizeof(ULONG) ||
        Isp->MailboxSize < Isp->Hardware->MailboxIrqEnable + sizeof(ULONG) ||
        Isp->Mailbox2Size < ISP_MBOX2_IRQ_ACK + sizeof(ULONG))
    {
        DbgPrint("appleisp: the mailbox or gpio registers are too small\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    DbgPrint("appleisp: coprocessor registers at %I64x mapped\n", Base);
    return STATUS_SUCCESS;
}

static
NTSTATUS
IspSetupAddressSpace(
    _In_ PISP_DEVICE Isp)
{
    PISP_REGION Region;
    ULONG64 End, Limit;
    ULONG Index;
    NTSTATUS Status;

    Status = AppleDartDomainCreate(&Isp->Tree, Isp->Tree.Node, "appleisp", &Isp->Dart);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleisp: the ISP DARTs could not be set up: %lx\n", Status);
        return Status;
    }
    Isp->DartReady = TRUE;
    for (Index = 0; Index < Isp->RegionCount; ++Index)
    {
        Region = &Isp->Regions[Index];
        Status = AppleDartDomainMap(&Isp->Dart, Region->Iova, Region->Physical, (SIZE_T)Region->Size);
        DbgPrint("appleisp: firmware region %s at %I64x+%I64x mapped at device address %I64x: %lx\n", Region->Name,
                 Region->Physical, Region->Size, Region->Iova, Status);
        if (!NT_SUCCESS(Status))
            return Status;
        Region->Mapped = TRUE;
    }
    End = (Isp->HeapTop & ~(ULONG64)MAXULONG) + ISP_SHARED_WINDOW_END;
    Limit = (Isp->HeapTop & ~(ULONG64)MAXULONG) + Isp->DartVmSize;
    End = min(End, Limit);
    if (End <= Isp->HeapTop || (Isp->HeapTop & (APPLE_DART_PAGE_SIZE - 1)))
    {
        DbgPrint("appleisp: no device address space above the firmware heap (%I64x, end %I64x)\n", Isp->HeapTop, End);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Isp->IovaBase = Isp->HeapTop;
    Isp->IovaPages = (ULONG)((End - Isp->HeapTop) / APPLE_DART_PAGE_SIZE);
    Isp->IovaBits = ExAllocatePoolZero(NonPagedPool, ((Isp->IovaPages + 31) / 32) * sizeof(ULONG), ISP_TAG);
    if (!Isp->IovaBits)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlInitializeBitMap(&Isp->IovaMap, Isp->IovaBits, Isp->IovaPages);
    RtlClearAllBits(&Isp->IovaMap);
    DbgPrint("appleisp: host buffers go at device addresses %I64x-%I64x\n", Isp->IovaBase, End);
    return STATUS_SUCCESS;
}

static
VOID
IspFindInterrupt(
    _Inout_ PISP_DEVICE Isp,
    _In_opt_ PCM_RESOURCE_LIST Translated)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Partial;
    ULONG Index;

    Isp->HaveInterrupt = FALSE;
    for (Index = 0; Translated && Translated->Count && Index < Translated->List[0].PartialResourceList.Count; ++Index)
    {
        Partial = &Translated->List[0].PartialResourceList.PartialDescriptors[Index];
        if (Partial->Type != CmResourceTypeInterrupt)
            continue;
        Isp->InterruptResource = *Partial;
        Isp->HaveInterrupt = TRUE;
        DbgPrint("appleisp: interrupt vector %lu level %lu assigned\n", Partial->u.Interrupt.Vector,
                 Partial->u.Interrupt.Level);
        return;
    }
    DbgPrint("appleisp: no interrupt was assigned, the IPC channels will be polled\n");
}

NTSTATUS
IspHardwareStart(
    _In_ PISP_DEVICE Isp,
    _In_opt_ PCM_RESOURCE_LIST Translated)
{
    HANDLE Thread;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(!Isp->HardwareStarted && Isp->TreeOpen && Isp->Hardware);
    Isp->HardwareStarted = TRUE;
    Status = ApplePmgrPowerOnDevice(&Isp->Tree, Isp->Tree.Node);
    DbgPrint("appleisp: ISP power domains on: %lx\n", Status);
    if (!NT_SUCCESS(Status))
        return Status;
    Isp->CorePowered = TRUE;
    Status = IspMapRegisters(Isp);
    if (!NT_SUCCESS(Status))
        return Status;
    Isp->Revision = IspCoprocRead(Isp, ISP_COPROC_VERSION);
    DbgPrint("appleisp: ISP revision %05lx, platform %lu, firmware %lu.%lu.%lu (compatible with %lu.%lu.%lu)\n",
             Isp->Revision, Isp->PlatformId, Isp->FirmwareVersion[0], Isp->FirmwareVersion[1],
             Isp->FirmwareVersion[2], Isp->FirmwareCompat[0], Isp->FirmwareCompat[1], Isp->FirmwareCompat[2]);
    Status = IspSetupAddressSpace(Isp);
    if (!NT_SUCCESS(Status))
        return Status;
    Isp->Ipc = IspAllocateSurface(Isp, ISP_IPC_SIZE, 0);
    Isp->Data = IspAllocateSurface(Isp, ISP_DATA_SIZE, 0);
    if (!Isp->Ipc || !Isp->Data)
        return STATUS_INSUFFICIENT_RESOURCES;
    DbgPrint("appleisp: IPC buffer at %I64x, data buffer at %I64x\n", Isp->Ipc->Iova, Isp->Data->Iova);
    KeInitializeDpc(&Isp->Dpc, IspDpc, Isp);
    IspFindInterrupt(Isp, Translated);
    Status = PsCreateSystemThread(&Thread, THREAD_ALL_ACCESS, NULL, NULL, NULL, IspWorker, Isp);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ObReferenceObjectByHandle(Thread, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, (PVOID *)&Isp->Worker,
                                       NULL);
    ZwClose(Thread);
    return Status;
}

NTSTATUS
IspDetect(
    _In_ PISP_DEVICE Isp)
{
    NTSTATUS Status;

    Status = IspFirmwareBoot(Isp);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = IspDetectSensor(Isp);
    IspFirmwareShutdown(Isp);
    return Status;
}

VOID
IspHardwareStop(
    _In_ PISP_DEVICE Isp)
{
    ULONG Index;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (!Isp->HardwareStarted)
        return;
    InterlockedExchange(&Isp->Streaming, 0);
    if (Isp->Gpio && Isp->Mailbox && Isp->Mailbox2 && Isp->CoprocWindow[ISP_COPROC_WINDOWS - 1])
        IspFirmwareShutdown(Isp);
    if (Isp->Worker)
    {
        InterlockedExchange(&Isp->StopWorker, 1);
        KeSetEvent(&Isp->WorkEvent, IO_NO_INCREMENT, FALSE);
        KeWaitForSingleObject(Isp->Worker, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Isp->Worker);
        Isp->Worker = NULL;
        InterlockedExchange(&Isp->StopWorker, 0);
    }
    IspDisconnectInterrupt(Isp);
    IspFreeSurface(Isp, Isp->Ipc);
    IspFreeSurface(Isp, Isp->Data);
    Isp->Ipc = Isp->Data = NULL;
    for (Index = 0; Index < Isp->RegionCount; ++Index)
    {
        if (Isp->Regions[Index].Mapped)
            AppleDartDomainUnmap(&Isp->Dart, Isp->Regions[Index].Iova, (SIZE_T)Isp->Regions[Index].Size);
        Isp->Regions[Index].Mapped = FALSE;
    }
    if (Isp->IovaBits)
        ExFreePoolWithTag(Isp->IovaBits, ISP_TAG);
    Isp->IovaBits = NULL;
    if (Isp->DartReady)
        AppleDartDomainDestroy(&Isp->Dart);
    Isp->DartReady = FALSE;
    for (Index = 0; Index < ISP_COPROC_WINDOWS; ++Index)
    {
        if (Isp->CoprocWindow[Index])
            MmUnmapIoSpace(Isp->CoprocWindow[Index], ISP_COPROC_WINDOW_SIZE);
        Isp->CoprocWindow[Index] = NULL;
    }
    if (Isp->Mailbox)
        MmUnmapIoSpace(Isp->Mailbox, Isp->MailboxSize);
    if (Isp->Gpio)
        MmUnmapIoSpace(Isp->Gpio, Isp->GpioSize);
    if (Isp->Mailbox2)
        MmUnmapIoSpace(Isp->Mailbox2, Isp->Mailbox2Size);
    Isp->Mailbox = Isp->Gpio = Isp->Mailbox2 = NULL;
    Status = ApplePmgrPowerOffDomains(&Isp->Tree, Isp->Tree.Node, 0);
    Isp->CorePowered = FALSE;
    DbgPrint("appleisp: ISP released and powered down: %lx\n", Status);
    Isp->HardwareStarted = FALSE;
}
