/*
 * PROJECT:     LiberNT Apple AOP Microphone Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AOP audio service, ADMAC capture channel and capture buffer of the built-in microphones
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

static ULONG64
AopMicNowMs()
{
    return KeQueryInterruptTime() / 10000;
}

CAopMicAdapter::CAopMicAdapter()
{
    KeInitializeMutex(&m_Mutex, 0);
    KeInitializeSpinLock(&m_Lock);
    KeInitializeTimerEx(&m_Timer, NotificationTimer);
    KeInitializeDpc(&m_Dpc, ServiceDpc, this);
    m_Cookie = 1;
}

CAopMicAdapter::~CAopMicAdapter()
{
    Shutdown();
}

NTSTATUS
NTAPI
CAopMicAdapter::QueryInterface(REFIID InterfaceId, PVOID *Interface)
{
    if (!Interface)
        return STATUS_INVALID_PARAMETER;
    if (IsEqualGUIDAligned(InterfaceId, IID_IUnknown))
    {
        *Interface = static_cast<PUNKNOWN>(this);
        AddRef();
        return STATUS_SUCCESS;
    }
    *Interface = NULL;
    return STATUS_NOINTERFACE;
}

NTSTATUS
CAopMicAdapter::QueryAopInterface(PDEVICE_OBJECT PhysicalDeviceObject)
{
    PIO_STACK_LOCATION Stack;
    IO_STATUS_BLOCK Io;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    RtlZeroMemory(&m_Aop, sizeof(m_Aop));
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, PhysicalDeviceObject, NULL, 0, NULL, &Event, &Io);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    Stack->Parameters.QueryInterface.InterfaceType = &GUID_APPLE_AOP_FUNCTION_INTERFACE;
    Stack->Parameters.QueryInterface.Size = sizeof(m_Aop);
    Stack->Parameters.QueryInterface.Version = APPLE_AOP_FUNCTION_INTERFACE_VERSION;
    Stack->Parameters.QueryInterface.Interface = reinterpret_cast<PINTERFACE>(&m_Aop);
    Stack->Parameters.QueryInterface.InterfaceSpecificData = NULL;
    Status = IoCallDriver(PhysicalDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Io.Status;
    }
    if (!NT_SUCCESS(Status))
        return Status;
    m_AopHeld = TRUE;
    if (m_Aop.Size < sizeof(m_Aop) || !m_Aop.Call || !m_Aop.InterfaceDereference ||
        m_Aop.Function != AppleAopMicrophones)
        return STATUS_NOT_SUPPORTED;
    return STATUS_SUCCESS;
}

NTSTATUS
CAopMicAdapter::Initialize(PDEVICE_OBJECT PhysicalDeviceObject)
{
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (!PhysicalDeviceObject)
        return STATUS_NO_SUCH_DEVICE;
    Status = QueryAopInterface(PhysicalDeviceObject);
    DbgPrint("appleaopmic: AOP audio service interface: %lx\n", Status);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = AopMicHwOpen(&m_Hardware);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = AopAudioConfigure(&m_Aop);
    DbgPrint("appleaopmic: AOP audio configured: %lx\n", Status);
    if (!NT_SUCCESS(Status))
        return Status;
    DbgPrint("appleaopmic: built-in microphones ready, %u channels of 32-bit float at %u Hz\n", AOP_MIC_CHANNELS,
             AOP_MIC_SAMPLE_RATE);
    return STATUS_SUCCESS;
}

VOID
CAopMicAdapter::Shutdown()
{
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    KeWaitForSingleObject(&m_Mutex, Executive, KernelMode, FALSE, NULL);
    StopLocked();
    if (m_StreamOpen && m_AopHeld)
        AopAudioSetPower(&m_Aop, AOP_AUDIO_POWER_IDLE, 1, NextCookie());
    if (m_ChannelOpen)
        AopMicHwCloseChannel(m_Hardware);
    m_ChannelOpen = FALSE;
    if (m_StreamOpen && m_AopHeld)
        AopAudioSetPower(&m_Aop, AOP_AUDIO_POWER_OFF, 0, NextCookie());
    m_StreamOpen = FALSE;
    AopMicHwClose(m_Hardware);
    m_Hardware = NULL;
    if (m_AopHeld)
        m_Aop.InterfaceDereference(m_Aop.Context);
    m_AopHeld = FALSE;
    KeReleaseMutex(&m_Mutex, FALSE);
}

ULONG
CAopMicAdapter::NextCookie()
{
    return static_cast<ULONG>(InterlockedIncrement(&m_Cookie) - 1);
}

NTSTATUS
CAopMicAdapter::OpenStream()
{
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    KeWaitForSingleObject(&m_Mutex, Executive, KernelMode, FALSE, NULL);
    if (m_StreamOpen || m_ChannelOpen)
    {
        KeReleaseMutex(&m_Mutex, FALSE);
        return STATUS_DEVICE_BUSY;
    }
    if (!m_Hardware || !m_AopHeld)
    {
        KeReleaseMutex(&m_Mutex, FALSE);
        return STATUS_DEVICE_NOT_READY;
    }
    Status = AopAudioSetPower(&m_Aop, AOP_AUDIO_POWER_IDLE, 0, NextCookie());
    if (NT_SUCCESS(Status))
    {
        Status = AopMicHwOpenChannel(m_Hardware, AOP_MIC_DMA_WORD_BYTES, AOP_MIC_DMA_FRAME_WORDS);
        m_ChannelOpen = NT_SUCCESS(Status);
    }
    if (NT_SUCCESS(Status))
        Status = AopAudioSetPower(&m_Aop, AOP_AUDIO_POWER_ON, 1, NextCookie());
    if (!NT_SUCCESS(Status))
    {
        if (m_ChannelOpen)
            AopMicHwCloseChannel(m_Hardware);
        m_ChannelOpen = FALSE;
        AopAudioSetPower(&m_Aop, AOP_AUDIO_POWER_OFF, 0, NextCookie());
        KeReleaseMutex(&m_Mutex, FALSE);
        return Status;
    }
    m_StreamOpen = TRUE;
    m_Position = 0;
    KeReleaseMutex(&m_Mutex, FALSE);
    return STATUS_SUCCESS;
}

VOID
CAopMicAdapter::CloseStream()
{
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    KeWaitForSingleObject(&m_Mutex, Executive, KernelMode, FALSE, NULL);
    if (!m_StreamOpen)
    {
        KeReleaseMutex(&m_Mutex, FALSE);
        return;
    }
    StopLocked();
    AopAudioSetPower(&m_Aop, AOP_AUDIO_POWER_IDLE, 1, NextCookie());
    if (m_ChannelOpen)
        AopMicHwCloseChannel(m_Hardware);
    m_ChannelOpen = FALSE;
    AopAudioSetPower(&m_Aop, AOP_AUDIO_POWER_OFF, 0, NextCookie());
    m_StreamOpen = FALSE;
    KeReleaseMutex(&m_Mutex, FALSE);
}

NTSTATUS
CAopMicAdapter::AllocateBuffer(
    ULONG NotificationCount,
    ULONG RequestedSize,
    PMDL *AudioBufferMdl,
    ULONG *ActualSize,
    ULONG *OffsetFromFirstPage,
    MEMORY_CACHING_TYPE *CacheType)
{
    PHYSICAL_ADDRESS Low, High, Skip;
    ULONG Period, Periods, Size, Pages, Index;
    PPFN_NUMBER Frames;
    PMDL Mdl;
    PVOID Buffer;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (!AudioBufferMdl || !ActualSize || !OffsetFromFirstPage || !CacheType)
        return STATUS_INVALID_PARAMETER;
    if (NotificationCount)
    {
        Period = (RequestedSize / NotificationCount) / AOP_MIC_FRAME_BYTES * AOP_MIC_FRAME_BYTES;
        Periods = NotificationCount;
    }
    else
    {
        Period = AOP_MIC_DEFAULT_PERIOD;
        Periods = max(RequestedSize / Period, 2UL);
    }
    if (Period < AOP_MIC_MIN_PERIOD || Period > AOP_MIC_MAX_PERIOD || Periods < 2 ||
        Periods > AOP_MIC_MAX_BUFFER / Period)
    {
        DbgPrint("appleaopmic: rejected a buffer of %lu bytes in %lu notifications\n", RequestedSize,
                 NotificationCount);
        return STATUS_INVALID_PARAMETER;
    }
    Size = Period * Periods;

    KeWaitForSingleObject(&m_Mutex, Executive, KernelMode, FALSE, NULL);
    if (m_Mdl)
    {
        KeReleaseMutex(&m_Mutex, FALSE);
        return STATUS_DEVICE_BUSY;
    }
    Low.QuadPart = 0;
    High.QuadPart = -1LL;
    Skip.QuadPart = 0;
    Mdl = MmAllocatePagesForMdlEx(Low, High, Skip, Size, MmNonCached,
                                  MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS | MM_ALLOCATE_FULLY_REQUIRED);
    if (!Mdl)
    {
        KeReleaseMutex(&m_Mutex, FALSE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Frames = MmGetMdlPfnArray(Mdl);
    Pages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(0, Size);
    for (Index = 1; Index < Pages; ++Index)
    {
        if (Frames[Index] != Frames[0] + Index)
            break;
    }
    Buffer = (Index == Pages) ? MmMapLockedPagesSpecifyCache(Mdl, KernelMode, MmNonCached, NULL, FALSE,
                                                             NormalPagePriority)
                              : NULL;
    if (!Buffer)
    {
        DbgPrint("appleaopmic: capture buffer of %lu bytes not mapped (page %lu of %lu contiguous)\n", Size, Index,
                 Pages);
        MmFreePagesFromMdl(Mdl);
        ExFreePool(Mdl);
        KeReleaseMutex(&m_Mutex, FALSE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Buffer, Size);
    m_Mdl = Mdl;
    m_Buffer = Buffer;
    m_BufferSize = Size;
    m_PeriodBytes = Period;
    m_BufferPhysical = (ULONG64)Frames[0] << PAGE_SHIFT;
    KeReleaseMutex(&m_Mutex, FALSE);
    DbgPrint("appleaopmic: capture buffer of %lu bytes in %lu byte periods at %I64x\n", Size, Period,
             m_BufferPhysical);
    *AudioBufferMdl = Mdl;
    *ActualSize = Size;
    *OffsetFromFirstPage = 0;
    *CacheType = MmNonCached;
    return STATUS_SUCCESS;
}

VOID
CAopMicAdapter::FreeBuffer(PMDL AudioBufferMdl)
{
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    KeWaitForSingleObject(&m_Mutex, Executive, KernelMode, FALSE, NULL);
    StopLocked();
    if (m_Mdl && AudioBufferMdl == m_Mdl)
    {
        MmUnmapLockedPages(m_Buffer, m_Mdl);
        MmFreePagesFromMdl(m_Mdl);
        ExFreePool(m_Mdl);
        m_Mdl = NULL;
        m_Buffer = NULL;
        m_BufferSize = 0;
        m_PeriodBytes = 0;
        m_BufferPhysical = 0;
    }
    KeReleaseMutex(&m_Mutex, FALSE);
}

VOID
CAopMicAdapter::StopLocked()
{
    ULONG Periods;
    KIRQL OldIrql;

    if (!m_Running)
        return;
    KeCancelTimer(&m_Timer);
    KeAcquireSpinLock(&m_Lock, &OldIrql);
    m_Position = AopMicHwPosition(m_Hardware);
    m_Running = FALSE;
    AopMicHwStop(m_Hardware);
    Periods = m_Periods;
    KeReleaseSpinLock(&m_Lock, OldIrql);
    KeFlushQueuedDpcs();
    DbgPrint("appleaopmic: capture stopped after %lu periods\n", Periods);
}

NTSTATUS
CAopMicAdapter::SetState(KSSTATE State)
{
    LARGE_INTEGER Due;
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    KeWaitForSingleObject(&m_Mutex, Executive, KernelMode, FALSE, NULL);
    if (State != KSSTATE_RUN)
    {
        StopLocked();
        if (State == KSSTATE_STOP)
            m_Position = 0;
        KeReleaseMutex(&m_Mutex, FALSE);
        return STATUS_SUCCESS;
    }
    if (m_Running)
    {
        KeReleaseMutex(&m_Mutex, FALSE);
        return STATUS_SUCCESS;
    }
    if (!m_ChannelOpen || !m_Mdl)
    {
        KeReleaseMutex(&m_Mutex, FALSE);
        return STATUS_INVALID_DEVICE_STATE;
    }
    KeAcquireSpinLock(&m_Lock, &OldIrql);
    /* FIXME: resuming from a pause restarts the capture DMA at the start of the buffer */
    Status = AopMicHwStart(m_Hardware, m_BufferPhysical, m_BufferSize, m_PeriodBytes);
    if (NT_SUCCESS(Status))
    {
        m_Running = TRUE;
        m_Periods = 0;
        m_FirstPeriodLogged = FALSE;
        m_StallLogged = FALSE;
        m_ProgressTime = AopMicNowMs();
    }
    KeReleaseSpinLock(&m_Lock, OldIrql);
    if (NT_SUCCESS(Status))
    {
        Due.QuadPart = -(LONGLONG)AOP_MIC_SERVICE_MS * 10000;
        /* TODO: connect the ADMAC interrupt instead of servicing the descriptor ring from a timer */
        KeSetTimerEx(&m_Timer, Due, AOP_MIC_SERVICE_MS, &m_Dpc);
    }
    DbgPrint("appleaopmic: capture start of %lu bytes in %lu byte periods: %lx\n", m_BufferSize, m_PeriodBytes,
             Status);
    KeReleaseMutex(&m_Mutex, FALSE);
    return Status;
}

NTSTATUS
CAopMicAdapter::GetPosition(PKSAUDIO_POSITION Position)
{
    ULONG Offset;
    KIRQL OldIrql;

    if (!Position)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&m_Lock, &OldIrql);
    Offset = m_Running ? AopMicHwPosition(m_Hardware) : m_Position;
    KeReleaseSpinLock(&m_Lock, OldIrql);
    Position->PlayOffset = Offset;
    Position->WriteOffset = Offset;
    return STATUS_SUCCESS;
}

NTSTATUS
CAopMicAdapter::RegisterNotificationEvent(PKEVENT NotificationEvent)
{
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!NotificationEvent)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&m_Lock, &OldIrql);
    if (m_Event && m_Event != NotificationEvent)
        Status = STATUS_DEVICE_BUSY;
    else
        m_Event = NotificationEvent;
    KeReleaseSpinLock(&m_Lock, OldIrql);
    return Status;
}

NTSTATUS
CAopMicAdapter::UnregisterNotificationEvent(PKEVENT NotificationEvent)
{
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;

    KeAcquireSpinLock(&m_Lock, &OldIrql);
    if (m_Event == NotificationEvent)
        m_Event = NULL;
    else
        Status = STATUS_INVALID_PARAMETER;
    KeReleaseSpinLock(&m_Lock, OldIrql);
    return Status;
}

VOID
NTAPI
CAopMicAdapter::ServiceDpc(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2)
{
    CAopMicAdapter *Adapter = static_cast<CAopMicAdapter *>(DeferredContext);
    PKEVENT Event = NULL;
    ULONG Reports, Position = 0, Periods = 0;
    ULONG64 Now = AopMicNowMs();
    BOOLEAN First = FALSE, Stalled = FALSE;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    KeAcquireSpinLockAtDpcLevel(&Adapter->m_Lock);
    if (!Adapter->m_Running)
    {
        KeReleaseSpinLockFromDpcLevel(&Adapter->m_Lock);
        return;
    }
    Reports = AopMicHwService(Adapter->m_Hardware);
    if (Reports)
    {
        Adapter->m_Periods += Reports;
        Adapter->m_ProgressTime = Now;
        Event = Adapter->m_Event;
        First = !Adapter->m_FirstPeriodLogged;
        Adapter->m_FirstPeriodLogged = TRUE;
    }
    else if (!Adapter->m_StallLogged && Now - Adapter->m_ProgressTime > AOP_MIC_STALL_MS)
    {
        Adapter->m_StallLogged = TRUE;
        Stalled = TRUE;
    }
    if (First || Stalled)
    {
        Position = AopMicHwPosition(Adapter->m_Hardware);
        Periods = Adapter->m_Periods;
    }
    KeReleaseSpinLockFromDpcLevel(&Adapter->m_Lock);
    if (Event)
        KeSetEvent(Event, IO_NO_INCREMENT, FALSE);
    if (First)
        DbgPrint("appleaopmic: first capture period done, position %lu\n", Position);
    if (Stalled)
        DbgPrint("appleaopmic: capture DMA made no progress for %u ms, position %lu after %lu periods\n",
                 AOP_MIC_STALL_MS, Position, Periods);
}
