/*
 * PROJECT:     LiberNT Apple AOP Microphone Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WaveRT and topology miniports of the built-in microphones
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

static KSDATARANGE_AUDIO AopMicCaptureDataRange =
{
    {{
        sizeof(KSDATARANGE_AUDIO),
        0,
        0,
        0,
        {STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO)},
        {STATICGUIDOF(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)},
        {STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)}
    }},
    AOP_MIC_CHANNELS,
    AOP_MIC_SAMPLE_BITS,
    AOP_MIC_SAMPLE_BITS,
    AOP_MIC_SAMPLE_RATE,
    AOP_MIC_SAMPLE_RATE
};

static PKSDATARANGE AopMicCaptureDataRanges[] =
{
    reinterpret_cast<PKSDATARANGE>(&AopMicCaptureDataRange)
};

static KSDATARANGE AopMicBridgeDataRange =
{{
    sizeof(KSDATARANGE),
    0,
    0,
    0,
    {STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO)},
    {STATICGUIDOF(KSDATAFORMAT_SUBTYPE_ANALOG)},
    {STATICGUIDOF(KSDATAFORMAT_SPECIFIER_NONE)}
}};

static PKSDATARANGE AopMicBridgeDataRanges[] =
{
    &AopMicBridgeDataRange
};

static PCPIN_DESCRIPTOR AopMicWavePins[] =
{
    {
        1, 1, 0, NULL,
        {
            0, NULL, 0, NULL,
            RTL_NUMBER_OF(AopMicCaptureDataRanges), AopMicCaptureDataRanges,
            KSPIN_DATAFLOW_OUT, KSPIN_COMMUNICATION_SINK, &KSCATEGORY_AUDIO, NULL, {0}
        }
    },
    {
        0, 0, 0, NULL,
        {
            0, NULL, 0, NULL,
            RTL_NUMBER_OF(AopMicBridgeDataRanges), AopMicBridgeDataRanges,
            KSPIN_DATAFLOW_IN, KSPIN_COMMUNICATION_NONE, &KSCATEGORY_AUDIO, NULL, {0}
        }
    }
};

static PCNODE_DESCRIPTOR AopMicWaveNodes[] =
{
    {0, NULL, &KSNODETYPE_ADC, NULL}
};

static PCCONNECTION_DESCRIPTOR AopMicWaveConnections[] =
{
    {PCFILTER_NODE, AOP_MIC_WAVE_BRIDGE_PIN, 0, 1},
    {0, 0, PCFILTER_NODE, AOP_MIC_WAVE_CAPTURE_PIN}
};

static PCFILTER_DESCRIPTOR AopMicWaveFilterDescriptor =
{
    0, NULL,
    sizeof(PCPIN_DESCRIPTOR), RTL_NUMBER_OF(AopMicWavePins), AopMicWavePins,
    sizeof(PCNODE_DESCRIPTOR), RTL_NUMBER_OF(AopMicWaveNodes), AopMicWaveNodes,
    RTL_NUMBER_OF(AopMicWaveConnections), AopMicWaveConnections,
    0, NULL
};

static PCPIN_DESCRIPTOR AopMicTopologyPins[] =
{
    {
        0, 0, 0, NULL,
        {
            0, NULL, 0, NULL,
            RTL_NUMBER_OF(AopMicBridgeDataRanges), AopMicBridgeDataRanges,
            KSPIN_DATAFLOW_IN, KSPIN_COMMUNICATION_NONE, &KSNODETYPE_MICROPHONE_ARRAY, NULL, {0}
        }
    },
    {
        0, 0, 0, NULL,
        {
            0, NULL, 0, NULL,
            RTL_NUMBER_OF(AopMicBridgeDataRanges), AopMicBridgeDataRanges,
            KSPIN_DATAFLOW_OUT, KSPIN_COMMUNICATION_NONE, &KSCATEGORY_AUDIO, NULL, {0}
        }
    }
};

static PCCONNECTION_DESCRIPTOR AopMicTopologyConnections[] =
{
    {PCFILTER_NODE, AOP_MIC_TOPOLOGY_MIC_PIN, PCFILTER_NODE, AOP_MIC_TOPOLOGY_WAVE_PIN}
};

static PCFILTER_DESCRIPTOR AopMicTopologyFilterDescriptor =
{
    0, NULL,
    sizeof(PCPIN_DESCRIPTOR), RTL_NUMBER_OF(AopMicTopologyPins), AopMicTopologyPins,
    sizeof(PCNODE_DESCRIPTOR), 0, NULL,
    RTL_NUMBER_OF(AopMicTopologyConnections), AopMicTopologyConnections,
    0, NULL
};

BOOLEAN
AopMicFormatValid(PKSDATAFORMAT DataFormat)
{
    PKSDATAFORMAT_WAVEFORMATEX WaveFormat;
    PWAVEFORMATEXTENSIBLE Extensible;

    if (!DataFormat || DataFormat->FormatSize < sizeof(KSDATAFORMAT_WAVEFORMATEX) ||
        !IsEqualGUIDAligned(DataFormat->MajorFormat, KSDATAFORMAT_TYPE_AUDIO) ||
        !IsEqualGUIDAligned(DataFormat->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) ||
        !IsEqualGUIDAligned(DataFormat->Specifier, KSDATAFORMAT_SPECIFIER_WAVEFORMATEX))
        return FALSE;
    WaveFormat = reinterpret_cast<PKSDATAFORMAT_WAVEFORMATEX>(DataFormat);
    if (WaveFormat->WaveFormatEx.wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        if (DataFormat->FormatSize < sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE) ||
            WaveFormat->WaveFormatEx.cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
            return FALSE;
        Extensible = reinterpret_cast<PWAVEFORMATEXTENSIBLE>(&WaveFormat->WaveFormatEx);
        if (!IsEqualGUIDAligned(Extensible->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) ||
            Extensible->Samples.wValidBitsPerSample != AOP_MIC_SAMPLE_BITS)
            return FALSE;
    }
    else if (WaveFormat->WaveFormatEx.wFormatTag != WAVE_FORMAT_IEEE_FLOAT)
    {
        return FALSE;
    }
    return WaveFormat->WaveFormatEx.nChannels == AOP_MIC_CHANNELS &&
           WaveFormat->WaveFormatEx.nSamplesPerSec == AOP_MIC_SAMPLE_RATE &&
           WaveFormat->WaveFormatEx.wBitsPerSample == AOP_MIC_SAMPLE_BITS &&
           WaveFormat->WaveFormatEx.nBlockAlign == AOP_MIC_FRAME_BYTES &&
           WaveFormat->WaveFormatEx.nAvgBytesPerSec == AOP_MIC_BYTES_PER_SECOND;
}

CAopMicTopology::CAopMicTopology(CAopMicAdapter *Adapter) : m_Adapter(Adapter)
{
    m_Adapter->AddRef();
}

CAopMicTopology::~CAopMicTopology()
{
    m_Adapter->Release();
}

NTSTATUS
NTAPI
CAopMicTopology::QueryInterface(REFIID InterfaceId, PVOID *Interface)
{
    if (!Interface)
        return STATUS_INVALID_PARAMETER;
    if (IsEqualGUIDAligned(InterfaceId, IID_IUnknown) || IsEqualGUIDAligned(InterfaceId, IID_IMiniport) ||
        IsEqualGUIDAligned(InterfaceId, IID_IMiniportTopology))
    {
        *Interface = static_cast<PMINIPORTTOPOLOGY>(this);
        AddRef();
        return STATUS_SUCCESS;
    }
    *Interface = NULL;
    return STATUS_NOINTERFACE;
}

NTSTATUS
NTAPI
CAopMicTopology::GetDescription(PPCFILTER_DESCRIPTOR *Description)
{
    if (!Description)
        return STATUS_INVALID_PARAMETER;
    *Description = &AopMicTopologyFilterDescriptor;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CAopMicTopology::DataRangeIntersection(
    ULONG PinId,
    PKSDATARANGE DataRange,
    PKSDATARANGE MatchingDataRange,
    ULONG OutputBufferLength,
    PVOID ResultantFormat,
    PULONG ResultantFormatLength)
{
    UNREFERENCED_PARAMETER(PinId);
    UNREFERENCED_PARAMETER(DataRange);
    UNREFERENCED_PARAMETER(MatchingDataRange);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(ResultantFormat);
    UNREFERENCED_PARAMETER(ResultantFormatLength);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
CAopMicTopology::Init(PUNKNOWN UnknownAdapter, PRESOURCELIST ResourceList, PPORTTOPOLOGY Port)
{
    UNREFERENCED_PARAMETER(UnknownAdapter);
    UNREFERENCED_PARAMETER(ResourceList);
    return Port ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

CAopMicWave::CAopMicWave(CAopMicAdapter *Adapter) : m_Adapter(Adapter)
{
    m_Adapter->AddRef();
}

CAopMicWave::~CAopMicWave()
{
    m_Adapter->Release();
}

NTSTATUS
NTAPI
CAopMicWave::QueryInterface(REFIID InterfaceId, PVOID *Interface)
{
    if (!Interface)
        return STATUS_INVALID_PARAMETER;
    if (IsEqualGUIDAligned(InterfaceId, IID_IUnknown) || IsEqualGUIDAligned(InterfaceId, IID_IMiniport) ||
        IsEqualGUIDAligned(InterfaceId, IID_IMiniportWaveRT))
    {
        *Interface = static_cast<PMINIPORTWAVERT>(this);
        AddRef();
        return STATUS_SUCCESS;
    }
    *Interface = NULL;
    return STATUS_NOINTERFACE;
}

NTSTATUS
NTAPI
CAopMicWave::GetDescription(PPCFILTER_DESCRIPTOR *Description)
{
    if (!Description)
        return STATUS_INVALID_PARAMETER;
    *Description = &AopMicWaveFilterDescriptor;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CAopMicWave::DataRangeIntersection(
    ULONG PinId,
    PKSDATARANGE DataRange,
    PKSDATARANGE MatchingDataRange,
    ULONG OutputBufferLength,
    PVOID ResultantFormat,
    PULONG ResultantFormatLength)
{
    PKSDATARANGE_AUDIO AudioRange;
    PKSDATAFORMAT_WAVEFORMATEXTENSIBLE Format;

    if (!ResultantFormatLength || !DataRange || !MatchingDataRange || PinId != AOP_MIC_WAVE_CAPTURE_PIN)
        return STATUS_INVALID_PARAMETER;
    if (!IsEqualGUIDAligned(DataRange->MajorFormat, KSDATAFORMAT_TYPE_AUDIO) ||
        !IsEqualGUIDAligned(DataRange->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) ||
        !IsEqualGUIDAligned(DataRange->Specifier, KSDATAFORMAT_SPECIFIER_WAVEFORMATEX))
        return STATUS_NO_MATCH;

    if (AopMicFormatValid(reinterpret_cast<PKSDATAFORMAT>(DataRange)))
    {
        *ResultantFormatLength = DataRange->FormatSize;
        if (!OutputBufferLength || !ResultantFormat)
            return STATUS_BUFFER_OVERFLOW;
        if (OutputBufferLength < DataRange->FormatSize)
            return STATUS_BUFFER_TOO_SMALL;
        RtlCopyMemory(ResultantFormat, DataRange, DataRange->FormatSize);
        return STATUS_SUCCESS;
    }

    if (DataRange->FormatSize != sizeof(KSDATARANGE_AUDIO))
        return STATUS_NO_MATCH;
    AudioRange = reinterpret_cast<PKSDATARANGE_AUDIO>(DataRange);
    if (AudioRange->MaximumChannels < AOP_MIC_CHANNELS || AudioRange->MinimumBitsPerSample > AOP_MIC_SAMPLE_BITS ||
        AudioRange->MaximumBitsPerSample < AOP_MIC_SAMPLE_BITS ||
        AudioRange->MinimumSampleFrequency > AOP_MIC_SAMPLE_RATE ||
        AudioRange->MaximumSampleFrequency < AOP_MIC_SAMPLE_RATE)
        return STATUS_NO_MATCH;

    *ResultantFormatLength = sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE);
    if (!OutputBufferLength || !ResultantFormat)
        return STATUS_BUFFER_OVERFLOW;
    if (OutputBufferLength < sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE))
        return STATUS_BUFFER_TOO_SMALL;
    Format = static_cast<PKSDATAFORMAT_WAVEFORMATEXTENSIBLE>(ResultantFormat);
    RtlZeroMemory(Format, sizeof(*Format));
    Format->DataFormat.FormatSize = sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE);
    Format->DataFormat.SampleSize = AOP_MIC_FRAME_BYTES;
    Format->DataFormat.MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
    Format->DataFormat.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    Format->DataFormat.Specifier = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;
    Format->WaveFormatExt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    Format->WaveFormatExt.Format.nChannels = AOP_MIC_CHANNELS;
    Format->WaveFormatExt.Format.nSamplesPerSec = AOP_MIC_SAMPLE_RATE;
    Format->WaveFormatExt.Format.wBitsPerSample = AOP_MIC_SAMPLE_BITS;
    Format->WaveFormatExt.Format.nBlockAlign = AOP_MIC_FRAME_BYTES;
    Format->WaveFormatExt.Format.nAvgBytesPerSec = AOP_MIC_BYTES_PER_SECOND;
    Format->WaveFormatExt.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    Format->WaveFormatExt.Samples.wValidBitsPerSample = AOP_MIC_SAMPLE_BITS;
    Format->WaveFormatExt.dwChannelMask = KSAUDIO_SPEAKER_DIRECTOUT;
    Format->WaveFormatExt.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CAopMicWave::Init(PUNKNOWN UnknownAdapter, PRESOURCELIST ResourceList, PPORTWAVERT Port)
{
    UNREFERENCED_PARAMETER(UnknownAdapter);
    UNREFERENCED_PARAMETER(ResourceList);
    return Port ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

NTSTATUS
NTAPI
CAopMicWave::NewStream(
    PMINIPORTWAVERTSTREAM *Stream,
    PPORTWAVERTSTREAM PortStream,
    ULONG Pin,
    BOOLEAN Capture,
    PKSDATAFORMAT DataFormat)
{
    CAopMicStream *NewStream;
    NTSTATUS Status;

    if (!Stream || !PortStream)
        return STATUS_INVALID_PARAMETER;
    if (Pin != AOP_MIC_WAVE_CAPTURE_PIN || !Capture || !AopMicFormatValid(DataFormat))
        return STATUS_NOT_SUPPORTED;
    Status = m_Adapter->OpenStream();
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleaopmic: capture stream not opened: %lx\n", Status);
        return Status;
    }
    NewStream = new (NonPagedPool, TAG_AOP_MIC) CAopMicStream(m_Adapter, PortStream);
    if (!NewStream)
    {
        m_Adapter->CloseStream();
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    NewStream->AddRef();
    *Stream = static_cast<PMINIPORTWAVERTSTREAM>(NewStream);
    DbgPrint("appleaopmic: capture stream opened\n");
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CAopMicWave::GetDeviceDescription(PDEVICE_DESCRIPTION DeviceDescription)
{
    if (!DeviceDescription)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(DeviceDescription, sizeof(*DeviceDescription));
    DeviceDescription->Version = DEVICE_DESCRIPTION_VERSION1;
    DeviceDescription->Master = TRUE;
    DeviceDescription->ScatterGather = FALSE;
    DeviceDescription->Dma64BitAddresses = TRUE;
    DeviceDescription->InterfaceType = Internal;
    DeviceDescription->MaximumLength = AOP_MIC_MAX_BUFFER;
    return STATUS_SUCCESS;
}

NTSTATUS
AopMicCreateTopology(PUNKNOWN *Unknown, CAopMicAdapter *Adapter)
{
    CAopMicTopology *Miniport;

    if (!Unknown || !Adapter)
        return STATUS_INVALID_PARAMETER;
    Miniport = new (NonPagedPool, TAG_AOP_MIC) CAopMicTopology(Adapter);
    if (!Miniport)
        return STATUS_INSUFFICIENT_RESOURCES;
    Miniport->AddRef();
    *Unknown = static_cast<PMINIPORTTOPOLOGY>(Miniport);
    return STATUS_SUCCESS;
}

NTSTATUS
AopMicCreateWave(PUNKNOWN *Unknown, CAopMicAdapter *Adapter)
{
    CAopMicWave *Miniport;

    if (!Unknown || !Adapter)
        return STATUS_INVALID_PARAMETER;
    Miniport = new (NonPagedPool, TAG_AOP_MIC) CAopMicWave(Adapter);
    if (!Miniport)
        return STATUS_INSUFFICIENT_RESOURCES;
    Miniport->AddRef();
    *Unknown = static_cast<PMINIPORTWAVERT>(Miniport);
    return STATUS_SUCCESS;
}
