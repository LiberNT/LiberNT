/*
 * PROJECT:     LiberNT Apple AOP Microphone Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WaveRT capture stream of the built-in microphones
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

CAopMicStream::CAopMicStream(CAopMicAdapter *Adapter, PPORTWAVERTSTREAM PortStream)
    : m_Adapter(Adapter), m_PortStream(PortStream), m_AudioBufferMdl(NULL), m_State(KSSTATE_STOP)
{
    m_Adapter->AddRef();
    m_PortStream->AddRef();
}

CAopMicStream::~CAopMicStream()
{
    m_Adapter->SetState(KSSTATE_STOP);
    if (m_AudioBufferMdl)
        m_Adapter->FreeBuffer(m_AudioBufferMdl);
    m_Adapter->CloseStream();
    m_PortStream->Release();
    m_Adapter->Release();
}

NTSTATUS
NTAPI
CAopMicStream::QueryInterface(REFIID InterfaceId, PVOID *Interface)
{
    if (!Interface)
        return STATUS_INVALID_PARAMETER;
    if (IsEqualGUIDAligned(InterfaceId, IID_IUnknown) || IsEqualGUIDAligned(InterfaceId, IID_IMiniportWaveRTStream) ||
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
CAopMicStream::SetFormat(PKSDATAFORMAT DataFormat)
{
    if (!AopMicFormatValid(DataFormat))
        return STATUS_NOT_SUPPORTED;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CAopMicStream::SetState(KSSTATE State)
{
    NTSTATUS Status;

    if (State < KSSTATE_STOP || State > KSSTATE_RUN)
        return STATUS_INVALID_PARAMETER;
    Status = m_Adapter->SetState(State);
    if (NT_SUCCESS(Status))
        m_State = State;
    return Status;
}

NTSTATUS
NTAPI
CAopMicStream::GetPosition(PKSAUDIO_POSITION Position)
{
    return m_Adapter->GetPosition(Position);
}

NTSTATUS
NTAPI
CAopMicStream::AllocateAudioBuffer(
    ULONG RequestedSize,
    PMDL *AudioBufferMdl,
    ULONG *ActualSize,
    ULONG *OffsetFromFirstPage,
    MEMORY_CACHING_TYPE *CacheType)
{
    NTSTATUS Status;

    if (m_AudioBufferMdl)
        return STATUS_DEVICE_BUSY;
    Status = m_Adapter->AllocateBuffer(0, RequestedSize, AudioBufferMdl, ActualSize, OffsetFromFirstPage, CacheType);
    if (NT_SUCCESS(Status))
        m_AudioBufferMdl = *AudioBufferMdl;
    return Status;
}

VOID
NTAPI
CAopMicStream::FreeAudioBuffer(PMDL AudioBufferMdl, ULONG BufferSize)
{
    UNREFERENCED_PARAMETER(BufferSize);

    if (m_AudioBufferMdl && AudioBufferMdl == m_AudioBufferMdl)
    {
        m_Adapter->FreeBuffer(m_AudioBufferMdl);
        m_AudioBufferMdl = NULL;
    }
}

VOID
NTAPI
CAopMicStream::GetHWLatency(PKSRTAUDIO_HWLATENCY HardwareLatency)
{
    if (!HardwareLatency)
        return;
    HardwareLatency->FifoSize = 0x30 * AOP_MIC_DMA_WORD_BYTES;
    HardwareLatency->ChipsetDelay = 0;
    HardwareLatency->CodecDelay = 0;
}

NTSTATUS
NTAPI
CAopMicStream::GetPositionRegister(PKSRTAUDIO_HWREGISTER Register)
{
    UNREFERENCED_PARAMETER(Register);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
CAopMicStream::GetClockRegister(PKSRTAUDIO_HWREGISTER Register)
{
    UNREFERENCED_PARAMETER(Register);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
CAopMicStream::AllocateBufferWithNotification(
    ULONG NotificationCount,
    ULONG RequestedSize,
    PMDL *AudioBufferMdl,
    ULONG *ActualSize,
    ULONG *OffsetFromFirstPage,
    MEMORY_CACHING_TYPE *CacheType)
{
    NTSTATUS Status;

    if (m_AudioBufferMdl)
        return STATUS_DEVICE_BUSY;
    if (!NotificationCount)
        return STATUS_INVALID_PARAMETER;
    Status = m_Adapter->AllocateBuffer(NotificationCount, RequestedSize, AudioBufferMdl, ActualSize,
                                       OffsetFromFirstPage, CacheType);
    if (NT_SUCCESS(Status))
        m_AudioBufferMdl = *AudioBufferMdl;
    return Status;
}

VOID
NTAPI
CAopMicStream::FreeBufferWithNotification(PMDL AudioBufferMdl, ULONG BufferSize)
{
    FreeAudioBuffer(AudioBufferMdl, BufferSize);
}

NTSTATUS
NTAPI
CAopMicStream::RegisterNotificationEvent(PKEVENT NotificationEvent)
{
    return m_Adapter->RegisterNotificationEvent(NotificationEvent);
}

NTSTATUS
NTAPI
CAopMicStream::UnregisterNotificationEvent(PKEVENT NotificationEvent)
{
    return m_Adapter->UnregisterNotificationEvent(NotificationEvent);
}
