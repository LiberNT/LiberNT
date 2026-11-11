/*
 * PROJECT:     LiberNT Apple AOP Microphone Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Internal definitions of the built-in microphone capture driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <ks.h>
#include <portcls.h>
#include <ksmedia.h>
#include "aopaudio.h"
#include "hardware.h"

#define TAG_AOP_MIC                 'mOpA'
#define AOP_MIC_CHANNELS            3
#define AOP_MIC_SAMPLE_BITS         32
#define AOP_MIC_SAMPLE_RATE         48000
#define AOP_MIC_FRAME_BYTES         (AOP_MIC_CHANNELS * AOP_MIC_SAMPLE_BITS / 8)
#define AOP_MIC_BYTES_PER_SECOND    (AOP_MIC_SAMPLE_RATE * AOP_MIC_FRAME_BYTES)
#define AOP_MIC_DMA_WORD_BYTES      4
#define AOP_MIC_DMA_FRAME_WORDS     4
#define AOP_MIC_MIN_PERIOD          256
#define AOP_MIC_MAX_PERIOD          0x4000
#define AOP_MIC_DEFAULT_PERIOD      (AOP_MIC_BYTES_PER_SECOND / 100)
#define AOP_MIC_MAX_BUFFER          (512 * 1024)
#define AOP_MIC_SERVICE_MS          5
#define AOP_MIC_STALL_MS            500
#define AOP_MIC_WAVE_CAPTURE_PIN    0
#define AOP_MIC_WAVE_BRIDGE_PIN     1
#define AOP_MIC_TOPOLOGY_MIC_PIN    0
#define AOP_MIC_TOPOLOGY_WAVE_PIN   1

PVOID
__cdecl
operator new(size_t Size, POOL_TYPE PoolType, ULONG Tag);

template <typename Interface> class CAopMicUnknown : public Interface
{
  private:
    volatile LONG m_RefCount;

  protected:
    CAopMicUnknown() : m_RefCount(0)
    {
    }

    virtual ~CAopMicUnknown()
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

class CAopMicAdapter : public CAopMicUnknown<IUnknown>
{
  public:
    CAopMicAdapter();
    virtual ~CAopMicAdapter();

    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);

    NTSTATUS Initialize(PDEVICE_OBJECT PhysicalDeviceObject, PRESOURCELIST ResourceList);
    VOID Shutdown();

    NTSTATUS OpenStream();
    VOID CloseStream();
    NTSTATUS AllocateBuffer(
        ULONG NotificationCount,
        ULONG RequestedSize,
        PMDL *AudioBufferMdl,
        ULONG *ActualSize,
        ULONG *OffsetFromFirstPage,
        MEMORY_CACHING_TYPE *CacheType);
    VOID FreeBuffer(PMDL AudioBufferMdl);
    NTSTATUS SetState(KSSTATE State);
    NTSTATUS GetPosition(PKSAUDIO_POSITION Position);
    NTSTATUS RegisterNotificationEvent(PKEVENT NotificationEvent);
    NTSTATUS UnregisterNotificationEvent(PKEVENT NotificationEvent);

  private:
    static VOID NTAPI ServiceDpc(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2);
    static NTSTATUS NTAPI InterruptService(PINTERRUPTSYNC InterruptSync, PVOID Context);
    static NTSTATUS NTAPI HardwareSynchronized(PINTERRUPTSYNC InterruptSync, PVOID Context);

    NTSTATUS QueryAopInterface(PDEVICE_OBJECT PhysicalDeviceObject);
    VOID StopLocked();
    ULONG NextCookie();
    NTSTATUS CallHardware(ULONG Operation, PULONG Value);

    APPLE_AOP_FUNCTION_INTERFACE m_Aop;
    BOOLEAN m_AopHeld;
    PAOP_MIC_HW m_Hardware;
    BOOLEAN m_ChannelOpen;
    KMUTEX m_Mutex;
    KSPIN_LOCK m_Lock;
    KTIMER m_Timer;
    KDPC m_Dpc;
    PINTERRUPTSYNC m_InterruptSync;
    volatile LONG m_Interrupts;
    volatile LONG m_PendingReports;
    BOOLEAN m_InterruptLogged;
    BOOLEAN m_StreamOpen;
    BOOLEAN m_Running;
    BOOLEAN m_FirstPeriodLogged;
    BOOLEAN m_StallLogged;
    volatile LONG m_Cookie;
    PKEVENT m_Event;
    PMDL m_Mdl;
    PVOID m_Buffer;
    ULONG64 m_BufferPhysical;
    ULONG m_BufferSize;
    ULONG m_PeriodBytes;
    ULONG m_Position;
    ULONG m_Periods;
    ULONG64 m_ProgressTime;
};

class CAopMicTopology : public CAopMicUnknown<IMiniportTopology>
{
  public:
    CAopMicTopology(CAopMicAdapter *Adapter);
    virtual ~CAopMicTopology();

    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);
    IMP_IMiniportTopology;

  private:
    CAopMicAdapter *m_Adapter;
};

class CAopMicWave : public CAopMicUnknown<IMiniportWaveRT>
{
  public:
    CAopMicWave(CAopMicAdapter *Adapter);
    virtual ~CAopMicWave();

    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);
    IMP_IMiniportWaveRT;

  private:
    CAopMicAdapter *m_Adapter;
};

class CAopMicStream : public CAopMicUnknown<IMiniportWaveRTStreamNotification>
{
  public:
    CAopMicStream(CAopMicAdapter *Adapter, PPORTWAVERTSTREAM PortStream);
    virtual ~CAopMicStream();

    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);
    IMP_IMiniportWaveRTStream;
    IMP_IMiniportWaveRTStreamNotification;

  private:
    CAopMicAdapter *m_Adapter;
    PPORTWAVERTSTREAM m_PortStream;
    PMDL m_AudioBufferMdl;
    KSSTATE m_State;
};

typedef struct _AOP_MIC_DEVICE_EXTENSION
{
    ULONG_PTR PortClassReserved[64];
    CAopMicAdapter *Adapter;
} AOP_MIC_DEVICE_EXTENSION, *PAOP_MIC_DEVICE_EXTENSION;

BOOLEAN
AopMicFormatValid(PKSDATAFORMAT DataFormat);

NTSTATUS
AopMicCreateTopology(PUNKNOWN *Unknown, CAopMicAdapter *Adapter);

NTSTATUS
AopMicCreateWave(PUNKNOWN *Unknown, CAopMicAdapter *Adapter);
