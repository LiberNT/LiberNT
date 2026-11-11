/*
 * PROJECT:         LiberNT Apple Silicon Audio Driver
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         WaveRT render stream
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

CAppleAudioStream::CAppleAudioStream(CAppleAudioAdapter *Adapter, PPORTWAVERTSTREAM PortStream, ULONG Direction,
                                     ULONG SampleBits)
    : m_Adapter(Adapter), m_PortStream(PortStream), m_AudioBufferMdl(NULL), m_State(KSSTATE_STOP),
      m_Direction(Direction), m_SampleBits(SampleBits)
{
    m_Adapter->AddRef();
    m_PortStream->AddRef();
}

CAppleAudioStream::~CAppleAudioStream()
{
    m_Adapter->SetStreamState(m_Direction, KSSTATE_STOP);
    if (m_AudioBufferMdl)
        m_Adapter->FreeBuffer(m_Direction, m_AudioBufferMdl);
    m_PortStream->Release();
    m_Adapter->ReleaseStream(m_Direction);
    m_Adapter->Release();
}

NTSTATUS
NTAPI
CAppleAudioStream::QueryInterface(REFIID InterfaceId, PVOID *Interface)
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
CAppleAudioStream::SetFormat(PKSDATAFORMAT DataFormat)
{
    ULONG SampleBits = AppleAudioFormatSampleBits(DataFormat, APPLE_AUDIO_STREAM_CHANNELS(m_Direction));

    if (!SampleBits)
        return STATUS_NOT_SUPPORTED;
    if (m_State != KSSTATE_STOP || (m_AudioBufferMdl && SampleBits != m_SampleBits))
        return STATUS_INVALID_DEVICE_STATE;
    m_SampleBits = SampleBits;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CAppleAudioStream::SetState(KSSTATE State)
{
    NTSTATUS Status;

    if (State < KSSTATE_STOP || State > KSSTATE_RUN)
        return STATUS_INVALID_PARAMETER;
    Status = m_Adapter->SetStreamState(m_Direction, State);
    if (NT_SUCCESS(Status))
        m_State = State;
    return Status;
}

NTSTATUS
NTAPI
CAppleAudioStream::GetPosition(PKSAUDIO_POSITION Position)
{
    return m_Adapter->GetPosition(m_Direction, Position);
}

NTSTATUS
NTAPI
CAppleAudioStream::AllocateAudioBuffer(
    ULONG RequestedSize,
    PMDL *AudioBufferMdl,
    ULONG *ActualSize,
    ULONG *OffsetFromFirstPage,
    MEMORY_CACHING_TYPE *CacheType)
{
    UNREFERENCED_PARAMETER(RequestedSize);
    UNREFERENCED_PARAMETER(AudioBufferMdl);
    UNREFERENCED_PARAMETER(ActualSize);
    UNREFERENCED_PARAMETER(OffsetFromFirstPage);
    UNREFERENCED_PARAMETER(CacheType);
    return STATUS_NOT_SUPPORTED;
}

VOID
NTAPI
CAppleAudioStream::FreeAudioBuffer(PMDL AudioBufferMdl, ULONG BufferSize)
{
    UNREFERENCED_PARAMETER(AudioBufferMdl);
    UNREFERENCED_PARAMETER(BufferSize);
}

VOID
NTAPI
CAppleAudioStream::GetHWLatency(PKSRTAUDIO_HWLATENCY HardwareLatency)
{
    if (!HardwareLatency)
        return;
    HardwareLatency->FifoSize = 0x30 * (m_SampleBits / 8);
    HardwareLatency->ChipsetDelay = 0;
    HardwareLatency->CodecDelay = 0;
}

NTSTATUS
NTAPI
CAppleAudioStream::GetPositionRegister(PKSRTAUDIO_HWREGISTER Register)
{
    UNREFERENCED_PARAMETER(Register);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
CAppleAudioStream::GetClockRegister(PKSRTAUDIO_HWREGISTER Register)
{
    UNREFERENCED_PARAMETER(Register);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
CAppleAudioStream::AllocateBufferWithNotification(
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
    Status = m_Adapter->AllocateBuffer(m_Direction, m_SampleBits, NotificationCount, RequestedSize, AudioBufferMdl,
                                       ActualSize, OffsetFromFirstPage, CacheType);
    if (NT_SUCCESS(Status))
        m_AudioBufferMdl = *AudioBufferMdl;
    return Status;
}

VOID
NTAPI
CAppleAudioStream::FreeBufferWithNotification(PMDL AudioBufferMdl, ULONG BufferSize)
{
    UNREFERENCED_PARAMETER(BufferSize);

    if (m_AudioBufferMdl && AudioBufferMdl == m_AudioBufferMdl)
    {
        m_Adapter->FreeBuffer(m_Direction, m_AudioBufferMdl);
        m_AudioBufferMdl = NULL;
    }
}

NTSTATUS
NTAPI
CAppleAudioStream::RegisterNotificationEvent(PKEVENT NotificationEvent)
{
    return m_Adapter->RegisterNotificationEvent(m_Direction, NotificationEvent);
}

NTSTATUS
NTAPI
CAppleAudioStream::UnregisterNotificationEvent(PKEVENT NotificationEvent)
{
    return m_Adapter->UnregisterNotificationEvent(m_Direction, NotificationEvent);
}
