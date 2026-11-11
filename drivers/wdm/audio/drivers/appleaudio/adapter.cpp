/*
 * PROJECT:         LiberNT Apple Silicon Audio Driver
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Adapter: stream control, DMA interrupt, volume, jack detection and speaker protection
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

typedef struct _APPLE_AUDIO_SYNC
{
    CAppleAudioAdapter *Adapter;
    PAPPLE_AUDIO_HW Hardware;
    ULONG Direction;
    ULONG64 Buffer;
    ULONG BufferSize;
    ULONG PeriodBytes;
    ULONG Position;
    NTSTATUS Status;
} APPLE_AUDIO_SYNC, *PAPPLE_AUDIO_SYNC;

CAppleAudioAdapter::CAppleAudioAdapter()
    : m_DeviceObject(NULL),
      m_Hardware(NULL),
      m_InterruptSync(NULL),
      m_JackWorkItem(NULL),
      m_JackWorkQueued(0),
      m_JackWorkAgain(0),
      m_JackInterrupt(NULL),
      m_JackInterrupts(0),
      m_ProtectWorkItem(NULL),
      m_ProtectWorkQueued(0),
      m_Shutdown(0),
      m_JackPortEvents(NULL),
      m_Connected(0),
      m_Microphone(0)
{
    ULONG Direction, Endpoint;

    KeInitializeMutex(&m_Mutex, 0);
    KeInitializeSpinLock(&m_EventLock);
    for (Direction = 0; Direction < APPLE_AUDIO_STREAMS; ++Direction)
    {
        KeInitializeDpc(&m_NotifyDpc[Direction], NotifyDpc, this);
        m_NotificationEvent[Direction] = NULL;
        m_Open[Direction] = 0;
        m_Running[Direction] = 0;
        m_Mdl[Direction] = NULL;
        m_Buffer[Direction] = NULL;
        m_BufferSize[Direction] = 0;
        m_PeriodBytes[Direction] = 0;
        m_BufferPhysical[Direction] = 0;
        m_SampleBits[Direction] = 0;
    }
    KeInitializeTimer(&m_JackTimer);
    KeInitializeDpc(&m_JackDpc, JackTimerDpc, this);
    AppleAudioJackPolicyInitialize(&m_JackPolicy);
    KeInitializeTimer(&m_ProtectTimer);
    KeInitializeDpc(&m_ProtectDpc, ProtectDpc, this);
    for (Endpoint = 0; Endpoint < APPLE_AUDIO_ENDPOINTS; ++Endpoint)
    {
        m_Volume[Endpoint][0] = m_Volume[Endpoint][1] = APPLE_AUDIO_VOLUME_DEFAULT * APPLE_AUDIO_KS_VOLUME_STEP;
        m_Mute[Endpoint] = FALSE;
    }
}

CAppleAudioAdapter::~CAppleAudioAdapter()
{
    Shutdown();
}

NTSTATUS
NTAPI
CAppleAudioAdapter::QueryInterface(REFIID InterfaceId, PVOID *Interface)
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
CAppleAudioAdapter::Initialize(PDEVICE_OBJECT DeviceObject, PDEVICE_OBJECT PhysicalDeviceObject, PRESOURCELIST ResourceList)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Raw;
    LARGE_INTEGER DueTime;
    BOOLEAN Connected, Microphone;
    ULONG Index, DmaIndex = MAXULONG, JackIndex = MAXULONG;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (!DeviceObject || !PhysicalDeviceObject || !ResourceList)
        return STATUS_INVALID_PARAMETER;
    m_DeviceObject = DeviceObject;
    if (!ResourceList->NumberOfInterrupts())
    {
        DbgPrint("appleaudio: PnP assigned no ADMAC interrupt\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Status = AppleAudioHwOpen(PhysicalDeviceObject, &m_Hardware);
    if (!NT_SUCCESS(Status))
        return Status;
    AppleAudioHwPollJack(m_Hardware, &Connected, &Microphone, NULL);
    InterlockedExchange(&m_Connected, Connected);
    InterlockedExchange(&m_Microphone, Microphone);

    for (Index = 0; Index < ResourceList->NumberOfInterrupts(); ++Index)
    {
        Raw = ResourceList->FindUntranslatedInterrupt(Index);
        if (!Raw)
            continue;
        if (DmaIndex == MAXULONG && AppleAudioHwIsDmaInterrupt(m_Hardware, Raw->u.Interrupt.Vector))
            DmaIndex = Index;
        else if (JackIndex == MAXULONG)
            JackIndex = Index;
    }
    if (DmaIndex == MAXULONG)
    {
        DbgPrint("appleaudio: PnP assigned no ADMAC interrupt\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Status = PcNewInterruptSync(&m_InterruptSync, NULL, ResourceList, DmaIndex, InterruptSyncModeNormal);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = m_InterruptSync->RegisterServiceRoutine(InterruptService, this, FALSE);
    if (NT_SUCCESS(Status))
        Status = m_InterruptSync->Connect();
    if (!NT_SUCCESS(Status))
        return Status;

    m_JackWorkItem = IoAllocateWorkItem(DeviceObject);
    if (!m_JackWorkItem)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (AppleAudioHwHasSpeakers(m_Hardware))
    {
        m_ProtectWorkItem = IoAllocateWorkItem(DeviceObject);
        if (!m_ProtectWorkItem)
            return STATUS_INSUFFICIENT_RESOURCES;
    }
    DueTime.QuadPart = -10000LL * m_JackPolicy.PollMs;
    KeSetTimerEx(&m_JackTimer, DueTime, m_JackPolicy.PollMs, &m_JackDpc);
    if (JackIndex != MAXULONG)
        ConnectJackInterrupt(ResourceList, JackIndex);
    else
        DbgPrint("appleaudio: no jack interrupt was assigned, the jack is polled\n");
    return STATUS_SUCCESS;
}

VOID
CAppleAudioAdapter::ConnectJackInterrupt(PRESOURCELIST ResourceList, ULONG Index)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Raw = ResourceList->FindUntranslatedInterrupt(Index);
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Translated = ResourceList->FindTranslatedInterrupt(Index);
    PKINTERRUPT Interrupt = NULL;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(m_Hardware && m_JackWorkItem && !m_JackInterrupt);
    if (!Raw || !Translated)
        return;
    Status = AppleAudioHwOpenJackInterrupt(m_Hardware, Raw->u.Interrupt.Vector);
    if (NT_SUCCESS(Status))
    {
        Status = IoConnectInterrupt(&Interrupt, JackService, this, NULL, Translated->u.Interrupt.Vector,
                                    (KIRQL)Translated->u.Interrupt.Level, (KIRQL)Translated->u.Interrupt.Level,
                                    (Translated->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ? Latched : LevelSensitive,
                                    Translated->ShareDisposition == CmResourceShareShared,
                                    Translated->u.Interrupt.Affinity, FALSE);
        if (!NT_SUCCESS(Status))
            AppleAudioHwCloseJackInterrupt(m_Hardware);
    }
    DbgPrint("appleaudio: jack interrupt %lu connected: %lx\n", Raw->u.Interrupt.Vector, Status);
    if (!NT_SUCCESS(Status))
        return;
    InterlockedExchangePointer(reinterpret_cast<PVOID volatile *>(&m_JackInterrupt), Interrupt);
    KeSynchronizeExecution(Interrupt, JackTestSynchronized, this);
}

BOOLEAN
NTAPI
CAppleAudioAdapter::JackService(PKINTERRUPT Interrupt, PVOID Context)
{
    CAppleAudioAdapter *Adapter = static_cast<CAppleAudioAdapter *>(Context);
    UNREFERENCED_PARAMETER(Interrupt);

    if (!Adapter->m_Hardware || !AppleAudioHwJackInterrupt(Adapter->m_Hardware))
        return FALSE;
    InterlockedIncrement(&Adapter->m_JackInterrupts);
    KeInsertQueueDpc(&Adapter->m_JackDpc, NULL, NULL);
    return TRUE;
}

BOOLEAN
NTAPI
CAppleAudioAdapter::JackArmSynchronized(PVOID Context)
{
    CAppleAudioAdapter *Adapter = static_cast<CAppleAudioAdapter *>(Context);

    AppleAudioHwArmJackInterrupt(Adapter->m_Hardware, FALSE);
    return TRUE;
}

BOOLEAN
NTAPI
CAppleAudioAdapter::JackTestSynchronized(PVOID Context)
{
    CAppleAudioAdapter *Adapter = static_cast<CAppleAudioAdapter *>(Context);

    AppleAudioHwArmJackInterrupt(Adapter->m_Hardware, TRUE);
    return TRUE;
}

VOID
CAppleAudioAdapter::JackInterruptWork(BOOLEAN Changed, BOOLEAN TipLatched)
{
    LARGE_INTEGER DueTime;
    ULONG Event, PollMs = m_JackPolicy.PollMs;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (!m_JackInterrupt)
        return;
    Event = AppleAudioJackPolicyUpdate(&m_JackPolicy, InterlockedCompareExchange(&m_JackInterrupts, 0, 0), Changed,
                                       TipLatched);
    switch (Event)
    {
        case APPLE_AUDIO_JACK_DELIVERY_PROVEN:
            DbgPrint("appleaudio: jack interrupt fired, jack polling slowed to %lu ms until the codec raises it\n",
                     m_JackPolicy.PollMs);
            break;
        case APPLE_AUDIO_JACK_CODEC_PROVEN:
            DbgPrint("appleaudio: the codec raised the jack interrupt, jack polling stopped\n");
            break;
        case APPLE_AUDIO_JACK_INTERRUPT_BROKEN:
            DbgPrint("appleaudio: the jack interrupt fired %lu times without a jack change, back to polling\n",
                     m_JackPolicy.Spurious);
            break;
        default:
            break;
    }
    if (m_JackPolicy.PollMs != PollMs)
    {
        if (m_JackPolicy.PollMs)
        {
            DueTime.QuadPart = -10000LL * m_JackPolicy.PollMs;
            KeSetTimerEx(&m_JackTimer, DueTime, m_JackPolicy.PollMs, &m_JackDpc);
        }
        else
        {
            KeCancelTimer(&m_JackTimer);
        }
    }
    if (!m_JackPolicy.Broken)
        KeSynchronizeExecution(m_JackInterrupt, JackArmSynchronized, this);
}

VOID
CAppleAudioAdapter::Shutdown()
{
    PPORTEVENTS PortEvents;
    LARGE_INTEGER Interval;

    if (InterlockedExchange(&m_Shutdown, 1))
        return;
    KeCancelTimer(&m_JackTimer);
    KeCancelTimer(&m_ProtectTimer);
    KeFlushQueuedDpcs();
    Interval.QuadPart = -10000LL;
    while (InterlockedCompareExchange(&m_JackWorkQueued, 0, 0) || InterlockedCompareExchange(&m_ProtectWorkQueued, 0, 0))
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
    if (m_JackInterrupt)
    {
        KeCancelTimer(&m_JackTimer);
        IoDisconnectInterrupt(m_JackInterrupt);
        m_JackInterrupt = NULL;
        KeFlushQueuedDpcs();
        DbgPrint("appleaudio: jack interrupt disconnected after %ld interrupts\n", m_JackInterrupts);
    }
    if (m_JackWorkItem)
    {
        IoFreeWorkItem(m_JackWorkItem);
        m_JackWorkItem = NULL;
    }

    KeWaitForSingleObject(&m_Mutex, Executive, KernelMode, FALSE, NULL);
    StopLocked(APPLE_AUDIO_SPEAKERS);
    StopLocked(APPLE_AUDIO_CAPTURE);
    StopLocked(APPLE_AUDIO_RENDER);
    KeReleaseMutex(&m_Mutex, FALSE);

    PortEvents = static_cast<PPORTEVENTS>(InterlockedExchangePointer(&m_JackPortEvents, NULL));
    if (PortEvents)
        PortEvents->Release();
    if (m_InterruptSync)
    {
        m_InterruptSync->Disconnect();
        m_InterruptSync->Release();
        m_InterruptSync = NULL;
    }
    KeFlushQueuedDpcs();
    while (InterlockedCompareExchange(&m_ProtectWorkQueued, 0, 0))
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
    if (m_ProtectWorkItem)
    {
        IoFreeWorkItem(m_ProtectWorkItem);
        m_ProtectWorkItem = NULL;
    }
    if (m_Hardware)
    {
        AppleAudioHwClose(m_Hardware);
        m_Hardware = NULL;
    }
}

NTSTATUS
CAppleAudioAdapter::RegisterJackEventPort(PPORTEVENTS PortEvents)
{
    PVOID Previous;

    if (!PortEvents || InterlockedCompareExchange(&m_Shutdown, 0, 0))
        return STATUS_INVALID_DEVICE_STATE;
    PortEvents->AddRef();
    Previous = InterlockedCompareExchangePointer(&m_JackPortEvents, PortEvents, NULL);
    if (Previous)
    {
        PortEvents->Release();
        return Previous == PortEvents ? STATUS_SUCCESS : STATUS_DEVICE_BUSY;
    }
    return STATUS_SUCCESS;
}

BOOLEAN
CAppleAudioAdapter::HasCapture()
{
    return m_Hardware && AppleAudioHwHasCapture(m_Hardware);
}

BOOLEAN
CAppleAudioAdapter::HasSpeakers()
{
    return m_Hardware && AppleAudioHwHasSpeakers(m_Hardware);
}

BOOLEAN
CAppleAudioAdapter::ClaimStream(ULONG Direction)
{
    return Direction < APPLE_AUDIO_STREAMS && InterlockedCompareExchange(&m_Open[Direction], 1, 0) == 0;
}

VOID
CAppleAudioAdapter::ReleaseStream(ULONG Direction)
{
    if (Direction < APPLE_AUDIO_STREAMS)
        InterlockedExchange(&m_Open[Direction], 0);
}

NTSTATUS
CAppleAudioAdapter::AllocateBuffer(
    ULONG Direction,
    ULONG SampleBits,
    ULONG NotificationCount,
    ULONG RequestedSize,
    PMDL *AudioBufferMdl,
    ULONG *ActualSize,
    ULONG *OffsetFromFirstPage,
    MEMORY_CACHING_TYPE *CacheType)
{
    PHYSICAL_ADDRESS Low, High, Skip;
    ULONG FrameBytes, Period, Pages, Index;
    PPFN_NUMBER Frames;
    PMDL Mdl;
    PVOID Buffer;

    if (Direction >= APPLE_AUDIO_STREAMS)
        return STATUS_INVALID_PARAMETER;
    FrameBytes = APPLE_AUDIO_STREAM_CHANNELS(Direction) * SampleBits / 8;
    if (!AudioBufferMdl || !ActualSize || !OffsetFromFirstPage || !CacheType || !FrameBytes)
        return STATUS_INVALID_PARAMETER;
    if (m_Mdl[Direction])
        return STATUS_DEVICE_BUSY;
    if (NotificationCount < 2 || NotificationCount > 64 || RequestedSize > APPLE_AUDIO_MAX_BUFFER_SIZE ||
        RequestedSize % NotificationCount)
    {
        DbgPrint("appleaudio: rejected buffer of %lu bytes in %lu notifications\n", RequestedSize, NotificationCount);
        return STATUS_INVALID_PARAMETER;
    }
    Period = RequestedSize / NotificationCount;
    if (Period < APPLE_AUDIO_MIN_PERIOD_SIZE || Period % FrameBytes)
    {
        DbgPrint("appleaudio: rejected period of %lu bytes for %lu-byte frames\n", Period, FrameBytes);
        return STATUS_INVALID_PARAMETER;
    }

    Low.QuadPart = 0;
    High.QuadPart = -1LL;
    Skip.QuadPart = 0;
    Mdl = MmAllocatePagesForMdlEx(Low, High, Skip, RequestedSize, MmNonCached,
                                  MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS | MM_ALLOCATE_FULLY_REQUIRED);
    if (!Mdl)
        return STATUS_INSUFFICIENT_RESOURCES;
    Frames = MmGetMdlPfnArray(Mdl);
    Pages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(0, RequestedSize);
    for (Index = 1; Index < Pages; ++Index)
    {
        if (Frames[Index] != Frames[0] + Index)
        {
            DbgPrint("appleaudio: buffer page %lu is not contiguous\n", Index);
            MmFreePagesFromMdl(Mdl);
            ExFreePool(Mdl);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    Buffer = MmMapLockedPagesSpecifyCache(Mdl, KernelMode, MmNonCached, NULL, FALSE, NormalPagePriority);
    if (!Buffer)
    {
        MmFreePagesFromMdl(Mdl);
        ExFreePool(Mdl);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Buffer, RequestedSize);

    m_Mdl[Direction] = Mdl;
    m_Buffer[Direction] = Buffer;
    m_BufferSize[Direction] = RequestedSize;
    m_PeriodBytes[Direction] = Period;
    m_BufferPhysical[Direction] = (ULONG64)Frames[0] << PAGE_SHIFT;
    m_SampleBits[Direction] = SampleBits;
    *AudioBufferMdl = Mdl;
    *ActualSize = RequestedSize;
    *OffsetFromFirstPage = 0;
    *CacheType = MmNonCached;
    return STATUS_SUCCESS;
}

VOID
CAppleAudioAdapter::FreeBuffer(ULONG Direction, PMDL AudioBufferMdl)
{
    if (Direction >= APPLE_AUDIO_STREAMS)
        return;
    KeWaitForSingleObject(&m_Mutex, Executive, KernelMode, FALSE, NULL);
    StopLocked(Direction);
    if (m_Mdl[Direction] && AudioBufferMdl == m_Mdl[Direction])
    {
        MmUnmapLockedPages(m_Buffer[Direction], m_Mdl[Direction]);
        MmFreePagesFromMdl(m_Mdl[Direction]);
        ExFreePool(m_Mdl[Direction]);
        m_Mdl[Direction] = NULL;
        m_Buffer[Direction] = NULL;
        m_BufferSize[Direction] = 0;
        m_PeriodBytes[Direction] = 0;
        m_BufferPhysical[Direction] = 0;
    }
    KeReleaseMutex(&m_Mutex, FALSE);
}

NTSTATUS
NTAPI
CAppleAudioAdapter::StartSynchronized(PINTERRUPTSYNC InterruptSync, PVOID Context)
{
    PAPPLE_AUDIO_SYNC Sync = static_cast<PAPPLE_AUDIO_SYNC>(Context);
    UNREFERENCED_PARAMETER(InterruptSync);

    Sync->Status = AppleAudioHwStartDma(Sync->Hardware, Sync->Direction, Sync->Buffer, Sync->BufferSize,
                                        Sync->PeriodBytes);
    if (NT_SUCCESS(Sync->Status))
        InterlockedExchange(&Sync->Adapter->m_Running[Sync->Direction], 1);
    return Sync->Status;
}

NTSTATUS
NTAPI
CAppleAudioAdapter::StopSynchronized(PINTERRUPTSYNC InterruptSync, PVOID Context)
{
    PAPPLE_AUDIO_SYNC Sync = static_cast<PAPPLE_AUDIO_SYNC>(Context);
    UNREFERENCED_PARAMETER(InterruptSync);

    InterlockedExchange(&Sync->Adapter->m_Running[Sync->Direction], 0);
    AppleAudioHwStopDma(Sync->Hardware, Sync->Direction);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CAppleAudioAdapter::PositionSynchronized(PINTERRUPTSYNC InterruptSync, PVOID Context)
{
    PAPPLE_AUDIO_SYNC Sync = static_cast<PAPPLE_AUDIO_SYNC>(Context);
    UNREFERENCED_PARAMETER(InterruptSync);

    Sync->Position = InterlockedCompareExchange(&Sync->Adapter->m_Running[Sync->Direction], 1, 1) ?
                     AppleAudioHwPosition(Sync->Hardware, Sync->Direction) : 0;
    return STATUS_SUCCESS;
}

VOID
CAppleAudioAdapter::StopLocked(ULONG Direction)
{
    APPLE_AUDIO_SYNC Sync;

    if (!m_Hardware || Direction >= APPLE_AUDIO_STREAMS)
        return;
    if (Direction == APPLE_AUDIO_SPEAKERS)
        KeCancelTimer(&m_ProtectTimer);
    if (m_InterruptSync && InterlockedCompareExchange(&m_Running[Direction], 1, 1))
    {
        RtlZeroMemory(&Sync, sizeof(Sync));
        Sync.Adapter = this;
        Sync.Hardware = m_Hardware;
        Sync.Direction = Direction;
        m_InterruptSync->CallSynchronizedRoutine(StopSynchronized, &Sync);
        KeFlushQueuedDpcs();
    }
    AppleAudioHwUnprepare(m_Hardware, Direction);
}

NTSTATUS
CAppleAudioAdapter::SetStreamState(ULONG Direction, KSSTATE State)
{
    APPLE_AUDIO_SYNC Sync;
    LARGE_INTEGER DueTime;
    NTSTATUS Status;

    if (Direction >= APPLE_AUDIO_STREAMS)
        return STATUS_INVALID_PARAMETER;
    KeWaitForSingleObject(&m_Mutex, Executive, KernelMode, FALSE, NULL);
    if (State != KSSTATE_RUN)
    {
        StopLocked(Direction);
        KeReleaseMutex(&m_Mutex, FALSE);
        return STATUS_SUCCESS;
    }
    if (!m_Hardware || !m_InterruptSync || !m_Mdl[Direction] || InterlockedCompareExchange(&m_Shutdown, 0, 0))
    {
        KeReleaseMutex(&m_Mutex, FALSE);
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (InterlockedCompareExchange(&m_Running[Direction], 1, 1))
    {
        KeReleaseMutex(&m_Mutex, FALSE);
        return STATUS_SUCCESS;
    }

    Status = AppleAudioHwPrepare(m_Hardware, Direction, m_SampleBits[Direction]);
    if (NT_SUCCESS(Status))
        Status = AppleAudioHwArm(m_Hardware, Direction);
    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(&Sync, sizeof(Sync));
        Sync.Adapter = this;
        Sync.Hardware = m_Hardware;
        Sync.Direction = Direction;
        Sync.Buffer = m_BufferPhysical[Direction];
        Sync.BufferSize = m_BufferSize[Direction];
        Sync.PeriodBytes = m_PeriodBytes[Direction];
        m_InterruptSync->CallSynchronizedRoutine(StartSynchronized, &Sync);
        Status = Sync.Status;
    }
    if (NT_SUCCESS(Status) && Direction == APPLE_AUDIO_SPEAKERS)
    {
        DueTime.QuadPart = -10000LL * APPLE_AUDIO_PROTECT_POLL_MS;
        KeSetTimerEx(&m_ProtectTimer, DueTime, APPLE_AUDIO_PROTECT_POLL_MS, &m_ProtectDpc);
    }
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleaudio: %s start failed 0x%08lx\n",
                 Direction == APPLE_AUDIO_CAPTURE ? "capture" :
                 Direction == APPLE_AUDIO_SPEAKERS ? "speaker playback" : "playback",
                 Status);
        AppleAudioHwUnprepare(m_Hardware, Direction);
    }
    KeReleaseMutex(&m_Mutex, FALSE);
    return Status;
}

NTSTATUS
CAppleAudioAdapter::GetPosition(ULONG Direction, PKSAUDIO_POSITION Position)
{
    APPLE_AUDIO_SYNC Sync;

    if (!Position || Direction >= APPLE_AUDIO_STREAMS)
        return STATUS_INVALID_PARAMETER;
    if (!m_Mdl[Direction] || !m_BufferSize[Direction])
        return STATUS_INVALID_DEVICE_STATE;
    RtlZeroMemory(&Sync, sizeof(Sync));
    Sync.Adapter = this;
    Sync.Hardware = m_Hardware;
    Sync.Direction = Direction;
    if (m_InterruptSync && m_Hardware)
        m_InterruptSync->CallSynchronizedRoutine(PositionSynchronized, &Sync);
    Position->PlayOffset = Sync.Position;
    Position->WriteOffset = Sync.Position;
    return STATUS_SUCCESS;
}

NTSTATUS
CAppleAudioAdapter::RegisterNotificationEvent(ULONG Direction, PKEVENT NotificationEvent)
{
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Direction >= APPLE_AUDIO_STREAMS)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&m_EventLock, &OldIrql);
    if (m_NotificationEvent[Direction] && m_NotificationEvent[Direction] != NotificationEvent)
        Status = STATUS_DEVICE_BUSY;
    else
        m_NotificationEvent[Direction] = NotificationEvent;
    KeReleaseSpinLock(&m_EventLock, OldIrql);
    return Status;
}

NTSTATUS
CAppleAudioAdapter::UnregisterNotificationEvent(ULONG Direction, PKEVENT NotificationEvent)
{
    KIRQL OldIrql;

    if (Direction >= APPLE_AUDIO_STREAMS)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&m_EventLock, &OldIrql);
    if (m_NotificationEvent[Direction] == NotificationEvent)
        m_NotificationEvent[Direction] = NULL;
    KeReleaseSpinLock(&m_EventLock, OldIrql);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CAppleAudioAdapter::InterruptService(PINTERRUPTSYNC InterruptSync, PVOID Context)
{
    CAppleAudioAdapter *Adapter = static_cast<CAppleAudioAdapter *>(Context);
    ULONG Periods[APPLE_AUDIO_DMA_CHANNELS], Direction;
    UNREFERENCED_PARAMETER(InterruptSync);

    if (!Adapter->m_Hardware || !AppleAudioHwInterrupt(Adapter->m_Hardware, Periods))
        return STATUS_UNSUCCESSFUL;
    for (Direction = 0; Direction < APPLE_AUDIO_STREAMS; ++Direction)
    {
        if (Periods[Direction] && InterlockedCompareExchange(&Adapter->m_Running[Direction], 1, 1))
            KeInsertQueueDpc(&Adapter->m_NotifyDpc[Direction], reinterpret_cast<PVOID>(ULONG_PTR(Direction)), NULL);
    }
    if (Periods[APPLE_AUDIO_SENSE])
        KeInsertQueueDpc(&Adapter->m_ProtectDpc, NULL, NULL);
    return STATUS_SUCCESS;
}

VOID
NTAPI
CAppleAudioAdapter::NotifyDpc(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2)
{
    CAppleAudioAdapter *Adapter = static_cast<CAppleAudioAdapter *>(DeferredContext);
    ULONG Direction = ULONG(reinterpret_cast<ULONG_PTR>(SystemArgument1));
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (Direction >= APPLE_AUDIO_STREAMS)
        return;
    KeAcquireSpinLockAtDpcLevel(&Adapter->m_EventLock);
    if (Adapter->m_NotificationEvent[Direction])
        KeSetEvent(Adapter->m_NotificationEvent[Direction], IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLockFromDpcLevel(&Adapter->m_EventLock);
}

VOID
NTAPI
CAppleAudioAdapter::JackTimerDpc(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2)
{
    CAppleAudioAdapter *Adapter = static_cast<CAppleAudioAdapter *>(DeferredContext);
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (InterlockedCompareExchange(&Adapter->m_Shutdown, 0, 0) || !Adapter->m_JackWorkItem)
        return;
    InterlockedExchange(&Adapter->m_JackWorkAgain, 1);
    if (InterlockedExchange(&Adapter->m_JackWorkQueued, 1))
        return;
    Adapter->AddRef();
    IoQueueWorkItem(Adapter->m_JackWorkItem, JackWork, DelayedWorkQueue, Adapter);
}

VOID
NTAPI
CAppleAudioAdapter::JackWork(PDEVICE_OBJECT DeviceObject, PVOID Context)
{
    CAppleAudioAdapter *Adapter = static_cast<CAppleAudioAdapter *>(Context);
    PPORTEVENTS PortEvents;
    BOOLEAN Changed, Connected, Microphone, TipLatched;
    UNREFERENCED_PARAMETER(DeviceObject);

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    for (;;)
    {
        InterlockedExchange(&Adapter->m_JackWorkAgain, 0);
        if (InterlockedCompareExchange(&Adapter->m_Shutdown, 0, 0))
        {
            InterlockedExchange(&Adapter->m_JackWorkQueued, 0);
            break;
        }
        Changed = Connected = Microphone = TipLatched = FALSE;
        KeWaitForSingleObject(&Adapter->m_Mutex, Executive, KernelMode, FALSE, NULL);
        if (Adapter->m_Hardware)
            Changed = AppleAudioHwPollJack(Adapter->m_Hardware, &Connected, &Microphone, &TipLatched);
        KeReleaseMutex(&Adapter->m_Mutex, FALSE);
        if (Changed)
        {
            InterlockedExchange(&Adapter->m_Connected, Connected);
            InterlockedExchange(&Adapter->m_Microphone, Microphone);
            PortEvents = static_cast<PPORTEVENTS>(InterlockedCompareExchangePointer(&Adapter->m_JackPortEvents, NULL, NULL));
            if (PortEvents)
            {
                PortEvents->GenerateEventList(const_cast<GUID *>(&KSEVENTSETID_PinCapsChange), KSEVENT_PINCAPS_JACKINFOCHANGE,
                                              TRUE, APPLE_AUDIO_TOPOLOGY_HEADPHONE_PIN, FALSE, ULONG(-1));
                PortEvents->GenerateEventList(const_cast<GUID *>(&KSEVENTSETID_PinCapsChange), KSEVENT_PINCAPS_JACKINFOCHANGE,
                                              TRUE, APPLE_AUDIO_TOPOLOGY_MICROPHONE_PIN, FALSE, ULONG(-1));
            }
        }
        Adapter->JackInterruptWork(Changed, TipLatched);
        InterlockedExchange(&Adapter->m_JackWorkQueued, 0);
        if (!InterlockedCompareExchange(&Adapter->m_JackWorkAgain, 0, 0) ||
            InterlockedExchange(&Adapter->m_JackWorkQueued, 1))
            break;
    }
    Adapter->Release();
}

VOID
NTAPI
CAppleAudioAdapter::ProtectDpc(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2)
{
    CAppleAudioAdapter *Adapter = static_cast<CAppleAudioAdapter *>(DeferredContext);
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (InterlockedCompareExchange(&Adapter->m_Shutdown, 0, 0) || !Adapter->m_ProtectWorkItem ||
        InterlockedExchange(&Adapter->m_ProtectWorkQueued, 1))
        return;
    Adapter->AddRef();
    IoQueueWorkItem(Adapter->m_ProtectWorkItem, ProtectWork, CriticalWorkQueue, Adapter);
}

VOID
NTAPI
CAppleAudioAdapter::ProtectWork(PDEVICE_OBJECT DeviceObject, PVOID Context)
{
    CAppleAudioAdapter *Adapter = static_cast<CAppleAudioAdapter *>(Context);
    UNREFERENCED_PARAMETER(DeviceObject);

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    KeWaitForSingleObject(&Adapter->m_Mutex, Executive, KernelMode, FALSE, NULL);
    if (Adapter->m_Hardware && !InterlockedCompareExchange(&Adapter->m_Shutdown, 0, 0))
        AppleAudioHwProtectSpeakers(Adapter->m_Hardware);
    KeReleaseMutex(&Adapter->m_Mutex, FALSE);
    InterlockedExchange(&Adapter->m_ProtectWorkQueued, 0);
    Adapter->Release();
}

NTSTATUS
CAppleAudioAdapter::ApplyVolumeLocked(ULONG Endpoint)
{
    if (!m_Hardware)
        return STATUS_INVALID_DEVICE_STATE;
    if (Endpoint == APPLE_AUDIO_ENDPOINT_SPEAKERS)
        return AppleAudioHwSetSpeakerVolume(m_Hardware, m_Volume[Endpoint][0] / APPLE_AUDIO_KS_VOLUME_STEP,
                                            m_Volume[Endpoint][1] / APPLE_AUDIO_KS_VOLUME_STEP, m_Mute[Endpoint]);
    return AppleAudioHwSetVolume(m_Hardware, m_Volume[Endpoint][0] / APPLE_AUDIO_KS_VOLUME_STEP,
                                 m_Volume[Endpoint][1] / APPLE_AUDIO_KS_VOLUME_STEP, m_Mute[Endpoint]);
}

NTSTATUS
CAppleAudioAdapter::GetVolume(ULONG Endpoint, ULONG Channel, PLONG Level)
{
    if (Endpoint >= APPLE_AUDIO_ENDPOINTS || Channel >= APPLE_AUDIO_CHANNELS || !Level)
        return STATUS_INVALID_PARAMETER;
    *Level = m_Volume[Endpoint][Channel];
    return STATUS_SUCCESS;
}

NTSTATUS
CAppleAudioAdapter::SetVolume(ULONG Endpoint, ULONG Channel, LONG Level)
{
    LONG Minimum;
    NTSTATUS Status;

    if (Endpoint >= APPLE_AUDIO_ENDPOINTS || (Channel >= APPLE_AUDIO_CHANNELS && Channel != ULONG(-1)))
        return STATUS_INVALID_PARAMETER;
    Minimum = (Endpoint == APPLE_AUDIO_ENDPOINT_SPEAKERS) ? APPLE_AUDIO_KS_SPEAKER_VOLUME_MINIMUM :
              APPLE_AUDIO_KS_VOLUME_MINIMUM;
    Level = min(max(Level, Minimum), APPLE_AUDIO_KS_VOLUME_MAXIMUM);
    Level = Minimum + ((Level - Minimum) / APPLE_AUDIO_KS_VOLUME_STEP) * APPLE_AUDIO_KS_VOLUME_STEP;
    KeWaitForSingleObject(&m_Mutex, Executive, KernelMode, FALSE, NULL);
    if (Channel == ULONG(-1))
        m_Volume[Endpoint][0] = m_Volume[Endpoint][1] = Level;
    else
        m_Volume[Endpoint][Channel] = Level;
    Status = ApplyVolumeLocked(Endpoint);
    KeReleaseMutex(&m_Mutex, FALSE);
    return Status;
}

BOOLEAN
CAppleAudioAdapter::GetMute(ULONG Endpoint)
{
    return Endpoint < APPLE_AUDIO_ENDPOINTS && m_Mute[Endpoint];
}

NTSTATUS
CAppleAudioAdapter::SetMute(ULONG Endpoint, BOOLEAN Mute)
{
    NTSTATUS Status;

    if (Endpoint >= APPLE_AUDIO_ENDPOINTS)
        return STATUS_INVALID_PARAMETER;
    KeWaitForSingleObject(&m_Mutex, Executive, KernelMode, FALSE, NULL);
    m_Mute[Endpoint] = Mute ? TRUE : FALSE;
    Status = ApplyVolumeLocked(Endpoint);
    KeReleaseMutex(&m_Mutex, FALSE);
    return Status;
}

BOOLEAN
CAppleAudioAdapter::IsSinkConnected()
{
    return InterlockedCompareExchange(&m_Connected, 0, 0) != 0;
}

BOOLEAN
CAppleAudioAdapter::IsMicrophoneConnected()
{
    return InterlockedCompareExchange(&m_Microphone, 0, 0) != 0;
}
