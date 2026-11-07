/*
 * PROJECT:         LiberNT Apple Silicon Audio Driver
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Adapter: stream control, DMA interrupt, volume and jack detection
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
      m_Shutdown(0),
      m_JackPortEvents(NULL),
      m_Mute(FALSE),
      m_Connected(0),
      m_Microphone(0)
{
    ULONG Direction;

    KeInitializeMutex(&m_Mutex, 0);
    KeInitializeSpinLock(&m_EventLock);
    for (Direction = 0; Direction < APPLE_AUDIO_DIRECTIONS; ++Direction)
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
    m_Volume[0] = m_Volume[1] = APPLE_AUDIO_VOLUME_DEFAULT * APPLE_AUDIO_KS_VOLUME_STEP;
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
    LARGE_INTEGER DueTime;
    BOOLEAN Connected, Microphone;
    NTSTATUS Status;

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
    AppleAudioHwPollJack(m_Hardware, &Connected, &Microphone);
    InterlockedExchange(&m_Connected, Connected);
    InterlockedExchange(&m_Microphone, Microphone);

    Status = PcNewInterruptSync(&m_InterruptSync, NULL, ResourceList, 0, InterruptSyncModeNormal);
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
    DueTime.QuadPart = -10000LL * APPLE_AUDIO_JACK_POLL_MS;
    KeSetTimerEx(&m_JackTimer, DueTime, APPLE_AUDIO_JACK_POLL_MS, &m_JackDpc);
    return STATUS_SUCCESS;
}

VOID
CAppleAudioAdapter::Shutdown()
{
    PPORTEVENTS PortEvents;
    LARGE_INTEGER Interval;

    if (InterlockedExchange(&m_Shutdown, 1))
        return;
    KeCancelTimer(&m_JackTimer);
    KeFlushQueuedDpcs();
    Interval.QuadPart = -10000LL;
    while (InterlockedCompareExchange(&m_JackWorkQueued, 0, 0))
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
    if (m_JackWorkItem)
    {
        IoFreeWorkItem(m_JackWorkItem);
        m_JackWorkItem = NULL;
    }

    KeWaitForSingleObject(&m_Mutex, Executive, KernelMode, FALSE, NULL);
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
CAppleAudioAdapter::ClaimStream(ULONG Direction)
{
    return Direction < APPLE_AUDIO_DIRECTIONS && InterlockedCompareExchange(&m_Open[Direction], 1, 0) == 0;
}

VOID
CAppleAudioAdapter::ReleaseStream(ULONG Direction)
{
    if (Direction < APPLE_AUDIO_DIRECTIONS)
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

    if (Direction >= APPLE_AUDIO_DIRECTIONS)
        return STATUS_INVALID_PARAMETER;
    FrameBytes = ((Direction == APPLE_AUDIO_RENDER) ? APPLE_AUDIO_CHANNELS : APPLE_AUDIO_CAPTURE_CHANNELS) * SampleBits / 8;
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
    if (Direction >= APPLE_AUDIO_DIRECTIONS)
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

    if (!m_Hardware || Direction >= APPLE_AUDIO_DIRECTIONS)
        return;
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
    NTSTATUS Status;

    if (Direction >= APPLE_AUDIO_DIRECTIONS)
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
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleaudio: %s start failed 0x%08lx\n", Direction == APPLE_AUDIO_RENDER ? "playback" : "capture",
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

    if (!Position || Direction >= APPLE_AUDIO_DIRECTIONS)
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

    if (Direction >= APPLE_AUDIO_DIRECTIONS)
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

    if (Direction >= APPLE_AUDIO_DIRECTIONS)
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
    ULONG Periods[APPLE_AUDIO_DIRECTIONS], Direction;
    UNREFERENCED_PARAMETER(InterruptSync);

    if (!Adapter->m_Hardware || !AppleAudioHwInterrupt(Adapter->m_Hardware, Periods))
        return STATUS_UNSUCCESSFUL;
    for (Direction = 0; Direction < APPLE_AUDIO_DIRECTIONS; ++Direction)
    {
        if (Periods[Direction] && InterlockedCompareExchange(&Adapter->m_Running[Direction], 1, 1))
            KeInsertQueueDpc(&Adapter->m_NotifyDpc[Direction], reinterpret_cast<PVOID>(ULONG_PTR(Direction)), NULL);
    }
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

    if (Direction >= APPLE_AUDIO_DIRECTIONS)
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

    if (InterlockedCompareExchange(&Adapter->m_Shutdown, 0, 0) || !Adapter->m_JackWorkItem ||
        InterlockedExchange(&Adapter->m_JackWorkQueued, 1))
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
    BOOLEAN Changed = FALSE, Connected = FALSE, Microphone = FALSE;
    UNREFERENCED_PARAMETER(DeviceObject);

    KeWaitForSingleObject(&Adapter->m_Mutex, Executive, KernelMode, FALSE, NULL);
    if (Adapter->m_Hardware && !InterlockedCompareExchange(&Adapter->m_Shutdown, 0, 0))
        Changed = AppleAudioHwPollJack(Adapter->m_Hardware, &Connected, &Microphone);
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
    InterlockedExchange(&Adapter->m_JackWorkQueued, 0);
    Adapter->Release();
}

NTSTATUS
CAppleAudioAdapter::ApplyVolumeLocked()
{
    if (!m_Hardware)
        return STATUS_INVALID_DEVICE_STATE;
    return AppleAudioHwSetVolume(m_Hardware, m_Volume[0] / APPLE_AUDIO_KS_VOLUME_STEP,
                                 m_Volume[1] / APPLE_AUDIO_KS_VOLUME_STEP, m_Mute);
}

NTSTATUS
CAppleAudioAdapter::GetVolume(ULONG Channel, PLONG Level)
{
    if (Channel >= APPLE_AUDIO_CHANNELS || !Level)
        return STATUS_INVALID_PARAMETER;
    *Level = m_Volume[Channel];
    return STATUS_SUCCESS;
}

NTSTATUS
CAppleAudioAdapter::SetVolume(ULONG Channel, LONG Level)
{
    NTSTATUS Status;

    if (Channel >= APPLE_AUDIO_CHANNELS && Channel != ULONG(-1))
        return STATUS_INVALID_PARAMETER;
    Level = min(max(Level, APPLE_AUDIO_KS_VOLUME_MINIMUM), APPLE_AUDIO_KS_VOLUME_MAXIMUM);
    Level = APPLE_AUDIO_KS_VOLUME_MINIMUM +
            ((Level - APPLE_AUDIO_KS_VOLUME_MINIMUM) / APPLE_AUDIO_KS_VOLUME_STEP) * APPLE_AUDIO_KS_VOLUME_STEP;
    KeWaitForSingleObject(&m_Mutex, Executive, KernelMode, FALSE, NULL);
    if (Channel == ULONG(-1))
        m_Volume[0] = m_Volume[1] = Level;
    else
        m_Volume[Channel] = Level;
    Status = ApplyVolumeLocked();
    KeReleaseMutex(&m_Mutex, FALSE);
    return Status;
}

BOOLEAN
CAppleAudioAdapter::GetMute()
{
    return m_Mute;
}

NTSTATUS
CAppleAudioAdapter::SetMute(BOOLEAN Mute)
{
    NTSTATUS Status;

    KeWaitForSingleObject(&m_Mutex, Executive, KernelMode, FALSE, NULL);
    m_Mute = Mute ? TRUE : FALSE;
    Status = ApplyVolumeLocked();
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
