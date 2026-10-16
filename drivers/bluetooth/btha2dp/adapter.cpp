/*
 * PROJECT:     LiberNT Bluetooth A2DP audio driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver entry, PortCls adapter and WaveRT render stream
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

PVOID
__cdecl
operator new(size_t Size, POOL_TYPE PoolType, ULONG Tag)
{
    PVOID Allocation = ExAllocatePoolWithTag(PoolType, Size, Tag);
    if (Allocation)
        RtlZeroMemory(Allocation, Size);
    return Allocation;
}

void
__cdecl
operator delete(PVOID Allocation)
{
    if (Allocation)
        ExFreePool(Allocation);
}

void
__cdecl
operator delete(PVOID Allocation, UINT_PTR)
{
    if (Allocation)
        ExFreePool(Allocation);
}

CBthA2dpAdapter::CBthA2dpAdapter() : m_Context(NULL), m_StreamOpen(0)
{
}

CBthA2dpAdapter::~CBthA2dpAdapter()
{
    if (m_Context)
        A2dpDestroy(m_Context);
}

NTSTATUS
NTAPI
CBthA2dpAdapter::QueryInterface(REFIID InterfaceId, PVOID *Interface)
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
CBthA2dpAdapter::Initialize(PDEVICE_OBJECT DeviceObject)
{
    PDEVICE_OBJECT PhysicalDevice;
    NTSTATUS Status;

    Status = PcGetPhysicalDeviceObject(DeviceObject, &PhysicalDevice);
    if (!NT_SUCCESS(Status))
        return Status;
    return A2dpCreate(PhysicalDevice, &m_Context);
}

VOID
CBthA2dpAdapter::Shutdown()
{
    if (m_Context)
        A2dpShutdown(m_Context);
}

BOOLEAN
CBthA2dpAdapter::ClaimStream()
{
    return InterlockedCompareExchange(&m_StreamOpen, 1, 0) == 0;
}

VOID
CBthA2dpAdapter::ReleaseStream()
{
    InterlockedExchange(&m_StreamOpen, 0);
}

CBthA2dpStream::CBthA2dpStream(CBthA2dpAdapter *Adapter, PPORTWAVERTSTREAM PortStream)
    : m_Adapter(Adapter), m_PortStream(PortStream), m_AudioBufferMdl(NULL), m_AudioBuffer(NULL), m_State(KSSTATE_STOP)
{
    m_Adapter->AddRef();
    m_PortStream->AddRef();
}

CBthA2dpStream::~CBthA2dpStream()
{
    A2dpSetRunning(m_Adapter->GetContext(), FALSE, TRUE);
    ReleaseBuffer();
    m_PortStream->Release();
    m_Adapter->ReleaseStream();
    m_Adapter->Release();
}

VOID
CBthA2dpStream::ReleaseBuffer()
{
    if (!m_AudioBufferMdl)
        return;
    A2dpDetachBuffer(m_Adapter->GetContext());
    if (m_AudioBuffer)
        m_PortStream->UnmapAllocatedPages(m_AudioBuffer, m_AudioBufferMdl);
    m_PortStream->FreePagesFromMdl(m_AudioBufferMdl);
    m_AudioBuffer = NULL;
    m_AudioBufferMdl = NULL;
}

NTSTATUS
NTAPI
CBthA2dpStream::QueryInterface(REFIID InterfaceId, PVOID *Interface)
{
    if (!Interface)
        return STATUS_INVALID_PARAMETER;
    if (IsEqualGUIDAligned(InterfaceId, IID_IUnknown) ||
        IsEqualGUIDAligned(InterfaceId, IID_IMiniportWaveRTStream) ||
        IsEqualGUIDAligned(InterfaceId, IID_IMiniportWaveRTStreamNotification))
    {
        *Interface = static_cast<PMINIPORTWAVERTSTREAMNOTIFICATION>(this);
        AddRef();
        return STATUS_SUCCESS;
    }
    *Interface = NULL;
    return STATUS_NOINTERFACE;
}

NTSTATUS
NTAPI
CBthA2dpStream::SetFormat(PKSDATAFORMAT DataFormat)
{
    if (m_State == KSSTATE_RUN)
        return STATUS_INVALID_DEVICE_STATE;
    return BthA2dpIsFormatSupported(DataFormat) ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
CBthA2dpStream::SetState(KSSTATE State)
{
    if (State < KSSTATE_STOP || State > KSSTATE_RUN)
        return STATUS_INVALID_PARAMETER;
    A2dpSetRunning(m_Adapter->GetContext(), State == KSSTATE_RUN, State == KSSTATE_STOP);
    m_State = State;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CBthA2dpStream::GetPosition(PKSAUDIO_POSITION Position)
{
    if (!Position)
        return STATUS_INVALID_PARAMETER;
    A2dpGetPosition(m_Adapter->GetContext(), &Position->PlayOffset, &Position->WriteOffset);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CBthA2dpStream::AllocateAudioBuffer(
    ULONG RequestedSize,
    PMDL *AudioBufferMdl,
    ULONG *ActualSize,
    ULONG *OffsetFromFirstPage,
    MEMORY_CACHING_TYPE *CacheType)
{
    return AllocateBufferWithNotification(1, RequestedSize, AudioBufferMdl, ActualSize, OffsetFromFirstPage, CacheType);
}

VOID
NTAPI
CBthA2dpStream::FreeAudioBuffer(PMDL AudioBufferMdl, ULONG BufferSize)
{
    FreeBufferWithNotification(AudioBufferMdl, BufferSize);
}

VOID
NTAPI
CBthA2dpStream::GetHWLatency(PKSRTAUDIO_HWLATENCY HardwareLatency)
{
    if (!HardwareLatency)
        return;
    HardwareLatency->FifoSize = 0;
    HardwareLatency->ChipsetDelay = 0;
    HardwareLatency->CodecDelay = 0;
}

NTSTATUS
NTAPI
CBthA2dpStream::GetPositionRegister(PKSRTAUDIO_HWREGISTER Register)
{
    UNREFERENCED_PARAMETER(Register);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
CBthA2dpStream::GetClockRegister(PKSRTAUDIO_HWREGISTER Register)
{
    UNREFERENCED_PARAMETER(Register);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
CBthA2dpStream::AllocateBufferWithNotification(
    ULONG NotificationCount,
    ULONG RequestedSize,
    PMDL *AudioBufferMdl,
    ULONG *ActualSize,
    ULONG *OffsetFromFirstPage,
    MEMORY_CACHING_TYPE *CacheType)
{
    PHYSICAL_ADDRESS HighAddress;
    ULONG Size;
    NTSTATUS Status;

    if (!AudioBufferMdl || !ActualSize || !OffsetFromFirstPage || !CacheType)
        return STATUS_INVALID_PARAMETER;
    if (m_AudioBufferMdl)
        return STATUS_DEVICE_BUSY;
    if (!NotificationCount || NotificationCount > 64 || !RequestedSize)
        return STATUS_INVALID_PARAMETER;
    Size = min(RequestedSize, BTHA2DP_MAX_BUFFER_SIZE);
    Size -= Size % (NotificationCount * BTHA2DP_BLOCK_ALIGN);
    if (!Size)
        return STATUS_INVALID_PARAMETER;

    HighAddress.QuadPart = -1;
    m_AudioBufferMdl = m_PortStream->AllocatePagesForMdl(HighAddress, Size);
    if (!m_AudioBufferMdl)
        return STATUS_INSUFFICIENT_RESOURCES;
    m_AudioBuffer = m_PortStream->MapAllocatedPages(m_AudioBufferMdl, MmCached);
    if (!m_AudioBuffer)
    {
        m_PortStream->FreePagesFromMdl(m_AudioBufferMdl);
        m_AudioBufferMdl = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(m_AudioBuffer, Size);
    Status = A2dpAttachBuffer(m_Adapter->GetContext(), static_cast<PUCHAR>(m_AudioBuffer), Size, NotificationCount);
    if (!NT_SUCCESS(Status))
    {
        m_PortStream->UnmapAllocatedPages(m_AudioBuffer, m_AudioBufferMdl);
        m_PortStream->FreePagesFromMdl(m_AudioBufferMdl);
        m_AudioBuffer = NULL;
        m_AudioBufferMdl = NULL;
        return Status;
    }
    *AudioBufferMdl = m_AudioBufferMdl;
    *ActualSize = Size;
    *OffsetFromFirstPage = 0;
    *CacheType = MmCached;
    return STATUS_SUCCESS;
}

VOID
NTAPI
CBthA2dpStream::FreeBufferWithNotification(PMDL AudioBufferMdl, ULONG BufferSize)
{
    UNREFERENCED_PARAMETER(BufferSize);
    if (AudioBufferMdl == m_AudioBufferMdl)
        ReleaseBuffer();
}

NTSTATUS
NTAPI
CBthA2dpStream::RegisterNotificationEvent(PKEVENT NotificationEvent)
{
    if (!NotificationEvent)
        return STATUS_INVALID_PARAMETER;
    return A2dpSetNotificationEvent(m_Adapter->GetContext(), NotificationEvent, TRUE);
}

NTSTATUS
NTAPI
CBthA2dpStream::UnregisterNotificationEvent(PKEVENT NotificationEvent)
{
    return A2dpSetNotificationEvent(m_Adapter->GetContext(), NotificationEvent, FALSE);
}

static
NTSTATUS
BthA2dpInstallSubdevice(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PCWSTR Name,
    REFGUID PortClassId,
    PUNKNOWN Miniport,
    CBthA2dpAdapter *Adapter,
    PRESOURCELIST ResourceList,
    PUNKNOWN *PortUnknown)
{
    PPORT Port;
    NTSTATUS Status;

    Status = PcNewPort(&Port, PortClassId);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = Port->Init(DeviceObject, Irp, Miniport, static_cast<PUNKNOWN>(Adapter), ResourceList);
    if (NT_SUCCESS(Status))
        Status = PcRegisterSubdevice(DeviceObject, const_cast<PWSTR>(Name), Port);
    if (NT_SUCCESS(Status) && PortUnknown)
        Status = Port->QueryInterface(IID_IUnknown, reinterpret_cast<PVOID *>(PortUnknown));
    Port->Release();
    return Status;
}

static
NTSTATUS
NTAPI
BthA2dpStartDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp, PRESOURCELIST ResourceList)
{
    PBTHA2DP_DEVICE_EXTENSION DeviceExtension = static_cast<PBTHA2DP_DEVICE_EXTENSION>(DeviceObject->DeviceExtension);
    CBthA2dpAdapter *Adapter;
    CBthA2dpTopology *Topology = NULL;
    CBthA2dpWave *Wave = NULL;
    PUNKNOWN TopologyPort = NULL, WavePort = NULL;
    NTSTATUS Status;

    Adapter = new (NonPagedPool, BTHA2DP_TAG) CBthA2dpAdapter();
    if (!Adapter)
        return STATUS_INSUFFICIENT_RESOURCES;
    Adapter->AddRef();
    Status = Adapter->Initialize(DeviceObject);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Topology = new (NonPagedPool, BTHA2DP_TAG) CBthA2dpTopology(Adapter);
    Wave = new (NonPagedPool, BTHA2DP_TAG) CBthA2dpWave(Adapter);
    if (!Topology || !Wave)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    Topology->AddRef();
    Wave->AddRef();
    Status = BthA2dpInstallSubdevice(DeviceObject, Irp, L"Topology", CLSID_PortTopology,
                                     static_cast<PMINIPORTTOPOLOGY>(Topology), Adapter, ResourceList, &TopologyPort);
    if (NT_SUCCESS(Status))
        Status = BthA2dpInstallSubdevice(DeviceObject, Irp, L"Wave", CLSID_PortWaveRT, static_cast<PMINIPORTWAVERT>(Wave),
                                         Adapter, ResourceList, &WavePort);
    if (NT_SUCCESS(Status))
        Status = PcRegisterPhysicalConnection(DeviceObject, WavePort, 1, TopologyPort, 0);
    if (NT_SUCCESS(Status))
    {
        DeviceExtension->Adapter = Adapter;
        Adapter->AddRef();
    }

Cleanup:
    if (WavePort)
        WavePort->Release();
    if (TopologyPort)
        TopologyPort->Release();
    if (Wave)
        Wave->Release();
    if (Topology)
        Topology->Release();
    Adapter->Release();
    return Status;
}

static
NTSTATUS
NTAPI
BthA2dpAddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject)
{
    return PcAddAdapterDevice(DriverObject, PhysicalDeviceObject, BthA2dpStartDevice, 2,
                              sizeof(BTHA2DP_DEVICE_EXTENSION));
}

static
NTSTATUS
NTAPI
BthA2dpPnp(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    PBTHA2DP_DEVICE_EXTENSION DeviceExtension = static_cast<PBTHA2DP_DEVICE_EXTENSION>(DeviceObject->DeviceExtension);

    if ((IoStack->MinorFunction == IRP_MN_REMOVE_DEVICE || IoStack->MinorFunction == IRP_MN_STOP_DEVICE ||
         IoStack->MinorFunction == IRP_MN_SURPRISE_REMOVAL) &&
        DeviceExtension->Adapter)
    {
        CBthA2dpAdapter *Adapter = DeviceExtension->Adapter;

        Adapter->Shutdown();
        if (IoStack->MinorFunction != IRP_MN_SURPRISE_REMOVAL)
        {
            DeviceExtension->Adapter = NULL;
            Adapter->Release();
        }
    }
    return PcDispatchIrp(DeviceObject, Irp);
}

extern "C"
NTSTATUS
NTAPI
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    NTSTATUS Status;

    Status = PcInitializeAdapterDriver(DriverObject, RegistryPath, BthA2dpAddDevice);
    if (NT_SUCCESS(Status))
        DriverObject->MajorFunction[IRP_MJ_PNP] = BthA2dpPnp;
    return Status;
}
