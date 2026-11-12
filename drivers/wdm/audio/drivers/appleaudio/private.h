/*
 * PROJECT:         LiberNT Apple Silicon Audio Driver
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Internal driver definitions
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <ks.h>
#include <portcls.h>
#include <ksmedia.h>
#include "hardware.h"
#include "jackpolicy.h"

#define TAG_APPLE_AUDIO 'aAPA'
#define APPLE_AUDIO_MAX_SUBDEVICES 4
#define APPLE_AUDIO_MAX_BUFFER_SIZE (512 * 1024)
#define APPLE_AUDIO_MIN_PERIOD_SIZE 256
#define APPLE_AUDIO_KS_VOLUME_STEP 0x8000
#define APPLE_AUDIO_KS_VOLUME_MINIMUM (APPLE_AUDIO_VOLUME_MINIMUM * APPLE_AUDIO_KS_VOLUME_STEP)
#define APPLE_AUDIO_KS_VOLUME_MAXIMUM (APPLE_AUDIO_VOLUME_MAXIMUM * APPLE_AUDIO_KS_VOLUME_STEP)
#define APPLE_AUDIO_KS_SPEAKER_VOLUME_MINIMUM (APPLE_AUDIO_SPEAKER_VOLUME_MINIMUM * APPLE_AUDIO_KS_VOLUME_STEP)
#define APPLE_AUDIO_TOPOLOGY_HEADPHONE_PIN 1
#define APPLE_AUDIO_TOPOLOGY_MICROPHONE_PIN 2
#define APPLE_AUDIO_TOPOLOGY_CAPTURE_PIN 3
#define APPLE_AUDIO_WAVE_RENDER_PIN 0
#define APPLE_AUDIO_WAVE_CAPTURE_PIN 2
#define APPLE_AUDIO_WAVE_CAPTURE_BRIDGE_PIN 3

#define APPLE_AUDIO_PROTECT_POLL_MS 50
#define APPLE_AUDIO_SPEAKER_TOPOLOGY_SPEAKER_PIN 1

PVOID
__cdecl
operator new(size_t Size, POOL_TYPE PoolType, ULONG Tag);

template <typename Interface> class CUnknownImpl : public Interface
{
  private:
    volatile LONG m_RefCount;

  protected:
    CUnknownImpl() : m_RefCount(0)
    {
    }

    virtual ~CUnknownImpl()
    {
    }

  public:
    STDMETHODIMP_(ULONG) AddRef()
    {
        return InterlockedIncrement(&m_RefCount);
    }

    STDMETHODIMP_(ULONG) Release()
    {
        ULONG RefCount = InterlockedDecrement(&m_RefCount);
        if (!RefCount)
            delete this;
        return RefCount;
    }
};

class CAppleAudioAdapter : public CUnknownImpl<IUnknown>
{
  public:
    CAppleAudioAdapter();
    virtual ~CAppleAudioAdapter();

    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);

    NTSTATUS Initialize(PDEVICE_OBJECT DeviceObject, PDEVICE_OBJECT PhysicalDeviceObject, PRESOURCELIST ResourceList);
    VOID Shutdown();
    NTSTATUS RegisterJackEventPort(PPORTEVENTS PortEvents);

    BOOLEAN HasCapture();
    BOOLEAN HasSpeakers();
    BOOLEAN ClaimStream(ULONG Direction);
    VOID ReleaseStream(ULONG Direction);

    NTSTATUS AllocateBuffer(
        ULONG Direction,
        ULONG SampleBits,
        ULONG NotificationCount,
        ULONG RequestedSize,
        PMDL *AudioBufferMdl,
        ULONG *ActualSize,
        ULONG *OffsetFromFirstPage,
        MEMORY_CACHING_TYPE *CacheType);
    VOID FreeBuffer(ULONG Direction, PMDL AudioBufferMdl);
    NTSTATUS SetStreamState(ULONG Direction, KSSTATE State);
    NTSTATUS GetPosition(ULONG Direction, PKSAUDIO_POSITION Position);
    NTSTATUS RegisterNotificationEvent(ULONG Direction, PKEVENT NotificationEvent);
    NTSTATUS UnregisterNotificationEvent(ULONG Direction, PKEVENT NotificationEvent);
    NTSTATUS GetVolume(ULONG Endpoint, ULONG Channel, PLONG Level);
    NTSTATUS SetVolume(ULONG Endpoint, ULONG Channel, LONG Level);
    BOOLEAN GetMute(ULONG Endpoint);
    NTSTATUS SetMute(ULONG Endpoint, BOOLEAN Mute);
    BOOLEAN IsSinkConnected();
    BOOLEAN IsMicrophoneConnected();

  private:
    static NTSTATUS NTAPI InterruptService(PINTERRUPTSYNC InterruptSync, PVOID Context);
    static NTSTATUS NTAPI StartSynchronized(PINTERRUPTSYNC InterruptSync, PVOID Context);
    static NTSTATUS NTAPI StopSynchronized(PINTERRUPTSYNC InterruptSync, PVOID Context);
    static NTSTATUS NTAPI PositionSynchronized(PINTERRUPTSYNC InterruptSync, PVOID Context);
    static VOID NTAPI NotifyDpc(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2);
    static VOID NTAPI JackTimerDpc(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2);
    static VOID NTAPI JackWork(PDEVICE_OBJECT DeviceObject, PVOID Context);
    static BOOLEAN NTAPI JackService(PKINTERRUPT Interrupt, PVOID Context);
    static BOOLEAN NTAPI JackArmSynchronized(PVOID Context);
    static BOOLEAN NTAPI JackTestSynchronized(PVOID Context);
    static VOID NTAPI ProtectDpc(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2);
    static VOID NTAPI ProtectWork(PDEVICE_OBJECT DeviceObject, PVOID Context);

    VOID StopLocked(ULONG Direction);
    NTSTATUS ApplyVolumeLocked(ULONG Endpoint);
    VOID ConnectJackInterrupt(PRESOURCELIST ResourceList, ULONG Index);
    VOID JackInterruptWork(BOOLEAN Changed, BOOLEAN TipLatched);

    PDEVICE_OBJECT m_DeviceObject;
    PAPPLE_AUDIO_HW m_Hardware;
    PINTERRUPTSYNC m_InterruptSync;
    KMUTEX m_Mutex;
    KDPC m_NotifyDpc[APPLE_AUDIO_STREAMS];
    KTIMER m_JackTimer;
    KDPC m_JackDpc;
    PIO_WORKITEM m_JackWorkItem;
    volatile LONG m_JackWorkQueued;
    volatile LONG m_JackWorkAgain;
    PKINTERRUPT m_JackInterrupt;
    volatile LONG m_JackInterrupts;
    APPLE_AUDIO_JACK_POLICY m_JackPolicy;
    KTIMER m_ProtectTimer;
    KDPC m_ProtectDpc;
    PIO_WORKITEM m_ProtectWorkItem;
    volatile LONG m_ProtectWorkQueued;
    KSPIN_LOCK m_EventLock;
    PKEVENT m_NotificationEvent[APPLE_AUDIO_STREAMS];
    volatile LONG m_Open[APPLE_AUDIO_STREAMS];
    volatile LONG m_Running[APPLE_AUDIO_STREAMS];
    volatile LONG m_Shutdown;
    PVOID volatile m_JackPortEvents;
    LONG m_Volume[APPLE_AUDIO_ENDPOINTS][APPLE_AUDIO_CHANNELS];
    BOOLEAN m_Mute[APPLE_AUDIO_ENDPOINTS];
    volatile LONG m_Connected;
    volatile LONG m_Microphone;
    PMDL m_Mdl[APPLE_AUDIO_STREAMS];
    PVOID m_Buffer[APPLE_AUDIO_STREAMS];
    ULONG m_BufferSize[APPLE_AUDIO_STREAMS];
    ULONG m_PeriodBytes[APPLE_AUDIO_STREAMS];
    ULONG64 m_BufferPhysical[APPLE_AUDIO_STREAMS];
    ULONG m_SampleBits[APPLE_AUDIO_STREAMS];
};

class CAppleAudioTopology : public CUnknownImpl<IMiniportTopology>
{
  public:
    CAppleAudioTopology(CAppleAudioAdapter *Adapter, ULONG Endpoint);
    virtual ~CAppleAudioTopology();

    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);
    IMP_IMiniportTopology;

    CAppleAudioAdapter *GetAdapter() const
    {
        return m_Adapter;
    }

    ULONG GetEndpoint() const
    {
        return m_Endpoint;
    }

    VOID AddEventToEventList(PKSEVENT_ENTRY EventEntry);

  private:
    CAppleAudioAdapter *m_Adapter;
    PPORTEVENTS m_PortEvents;
    ULONG m_Endpoint;
};

class CAppleAudioWave : public CUnknownImpl<IMiniportWaveRT>
{
  public:
    CAppleAudioWave(CAppleAudioAdapter *Adapter, ULONG Endpoint);
    virtual ~CAppleAudioWave();

    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);
    IMP_IMiniportWaveRT;

  private:
    CAppleAudioAdapter *m_Adapter;
    ULONG m_Endpoint;
};

class CAppleAudioStream : public CUnknownImpl<IMiniportWaveRTStreamNotification>
{
  public:
    CAppleAudioStream(CAppleAudioAdapter *Adapter, PPORTWAVERTSTREAM PortStream, ULONG Direction, ULONG SampleBits);
    virtual ~CAppleAudioStream();

    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);
    IMP_IMiniportWaveRTStream;
    IMP_IMiniportWaveRTStreamNotification;

  private:
    CAppleAudioAdapter *m_Adapter;
    PPORTWAVERTSTREAM m_PortStream;
    PMDL m_AudioBufferMdl;
    KSSTATE m_State;
    ULONG m_Direction;
    ULONG m_SampleBits;
};

typedef struct _APPLE_AUDIO_DEVICE_EXTENSION
{
    ULONG_PTR PortClassReserved[64];
    CAppleAudioAdapter *Adapter;
} APPLE_AUDIO_DEVICE_EXTENSION, *PAPPLE_AUDIO_DEVICE_EXTENSION;

ULONG
AppleAudioFormatSampleBits(PKSDATAFORMAT DataFormat, ULONG Channels);

NTSTATUS
AppleAudioCreateTopology(PUNKNOWN *Unknown, CAppleAudioAdapter *Adapter, ULONG Endpoint);

NTSTATUS
AppleAudioCreateWave(PUNKNOWN *Unknown, CAppleAudioAdapter *Adapter, ULONG Endpoint);
