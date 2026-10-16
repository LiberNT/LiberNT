/*
 * PROJECT:     LiberNT Bluetooth A2DP audio driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Audio miniport definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <ks.h>
#include <portcls.h>
#include <ksmedia.h>
#include "btha2dp.h"

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

class CBthA2dpAdapter : public CUnknownImpl<IUnknown>
{
  public:
    CBthA2dpAdapter();
    virtual ~CBthA2dpAdapter();

    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);

    NTSTATUS Initialize(PDEVICE_OBJECT DeviceObject);
    VOID Shutdown();
    BOOLEAN ClaimStream();
    VOID ReleaseStream();

    PA2DP_CONTEXT GetContext() const
    {
        return m_Context;
    }

  private:
    PA2DP_CONTEXT m_Context;
    volatile LONG m_StreamOpen;
};

class CBthA2dpTopology : public CUnknownImpl<IMiniportTopology>
{
  public:
    explicit CBthA2dpTopology(CBthA2dpAdapter *Adapter);
    virtual ~CBthA2dpTopology();

    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);
    IMP_IMiniportTopology;

    CBthA2dpAdapter *GetAdapter() const
    {
        return m_Adapter;
    }

  private:
    CBthA2dpAdapter *m_Adapter;
};

class CBthA2dpWave : public CUnknownImpl<IMiniportWaveRT>
{
  public:
    explicit CBthA2dpWave(CBthA2dpAdapter *Adapter);
    virtual ~CBthA2dpWave();

    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);
    IMP_IMiniportWaveRT;

  private:
    CBthA2dpAdapter *m_Adapter;
};

class CBthA2dpStream : public CUnknownImpl<IMiniportWaveRTStreamNotification>
{
  public:
    CBthA2dpStream(CBthA2dpAdapter *Adapter, PPORTWAVERTSTREAM PortStream);
    virtual ~CBthA2dpStream();

    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);
    IMP_IMiniportWaveRTStream;
    IMP_IMiniportWaveRTStreamNotification;

  private:
    VOID ReleaseBuffer();

    CBthA2dpAdapter *m_Adapter;
    PPORTWAVERTSTREAM m_PortStream;
    PMDL m_AudioBufferMdl;
    PVOID m_AudioBuffer;
    KSSTATE m_State;
};

typedef struct _BTHA2DP_DEVICE_EXTENSION
{
    ULONG_PTR PortClassReserved[64];
    CBthA2dpAdapter *Adapter;
} BTHA2DP_DEVICE_EXTENSION, *PBTHA2DP_DEVICE_EXTENSION;

extern PCFILTER_DESCRIPTOR BthA2dpWaveFilterDescriptor;
extern PCFILTER_DESCRIPTOR BthA2dpTopologyFilterDescriptor;

BOOLEAN
BthA2dpIsFormatSupported(PKSDATAFORMAT DataFormat);
