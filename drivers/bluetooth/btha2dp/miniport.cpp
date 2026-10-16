/*
 * PROJECT:     LiberNT Bluetooth A2DP audio driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WaveRT and topology miniports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

static KSDATARANGE_AUDIO BthA2dpPcmDataRange =
{
    {{
        sizeof(KSDATARANGE_AUDIO),
        0,
        BTHA2DP_BLOCK_ALIGN,
        0,
        {STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO)},
        {STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM)},
        {STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)}
    }},
    BTHA2DP_CHANNELS,
    BTHA2DP_BITS_PER_SAMPLE,
    BTHA2DP_BITS_PER_SAMPLE,
    BTHA2DP_SAMPLE_RATE,
    BTHA2DP_SAMPLE_RATE
};

static PKSDATARANGE BthA2dpPcmDataRanges[] =
{
    reinterpret_cast<PKSDATARANGE>(&BthA2dpPcmDataRange)
};

static KSDATARANGE BthA2dpBridgeDataRange =
{{
    sizeof(KSDATARANGE),
    0,
    0,
    0,
    {STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO)},
    {STATICGUIDOF(KSDATAFORMAT_SUBTYPE_ANALOG)},
    {STATICGUIDOF(KSDATAFORMAT_SPECIFIER_NONE)}
}};

static PKSDATARANGE BthA2dpBridgeDataRanges[] =
{
    &BthA2dpBridgeDataRange
};

static
NTSTATUS
BthA2dpBasicSupport(
    PPCPROPERTY_REQUEST PropertyRequest,
    ULONG PropertyType,
    LONG Minimum,
    LONG Maximum,
    ULONG Step)
{
    const ULONG DescriptionSize = sizeof(KSPROPERTY_DESCRIPTION) + sizeof(KSPROPERTY_MEMBERSHEADER) +
                                  BTHA2DP_CHANNELS * sizeof(KSPROPERTY_STEPPING_LONG);
    ULONG AccessFlags = KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET;
    PKSPROPERTY_DESCRIPTION Description;
    PKSPROPERTY_MEMBERSHEADER Members;
    PKSPROPERTY_STEPPING_LONG Range;
    ULONG Index;

    if (PropertyRequest->ValueSize < sizeof(ULONG))
    {
        PropertyRequest->ValueSize = sizeof(ULONG);
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (PropertyRequest->ValueSize < sizeof(KSPROPERTY_DESCRIPTION))
    {
        *static_cast<PULONG>(PropertyRequest->Value) = AccessFlags;
        PropertyRequest->ValueSize = sizeof(ULONG);
        return STATUS_SUCCESS;
    }
    Description = static_cast<PKSPROPERTY_DESCRIPTION>(PropertyRequest->Value);
    RtlZeroMemory(Description, sizeof(*Description));
    Description->AccessFlags = AccessFlags;
    Description->DescriptionSize = DescriptionSize;
    Description->PropTypeSet.Set = KSPROPTYPESETID_General;
    Description->PropTypeSet.Id = PropertyType;
    Description->MembersListCount = 1;
    if (PropertyRequest->ValueSize < DescriptionSize)
    {
        PropertyRequest->ValueSize = sizeof(KSPROPERTY_DESCRIPTION);
        return STATUS_SUCCESS;
    }
    Members = reinterpret_cast<PKSPROPERTY_MEMBERSHEADER>(Description + 1);
    Members->MembersFlags = KSPROPERTY_MEMBER_STEPPEDRANGES;
    Members->MembersSize = sizeof(KSPROPERTY_STEPPING_LONG);
    Members->MembersCount = BTHA2DP_CHANNELS;
    Members->Flags = KSPROPERTY_MEMBER_FLAG_BASICSUPPORT_MULTICHANNEL;
    Range = reinterpret_cast<PKSPROPERTY_STEPPING_LONG>(Members + 1);
    for (Index = 0; Index < BTHA2DP_CHANNELS; Index++)
    {
        Range[Index].SteppingDelta = Step;
        Range[Index].Bounds.SignedMinimum = Minimum;
        Range[Index].Bounds.SignedMaximum = Maximum;
    }
    PropertyRequest->ValueSize = DescriptionSize;
    return STATUS_SUCCESS;
}

static
PA2DP_CONTEXT
BthA2dpTopologyContext(
    PPCPROPERTY_REQUEST PropertyRequest)
{
    CBthA2dpTopology *Topology = static_cast<CBthA2dpTopology *>(
        static_cast<PMINIPORTTOPOLOGY>(PropertyRequest->MajorTarget));

    return Topology->GetAdapter()->GetContext();
}

static
NTSTATUS
NTAPI
BthA2dpVolumeHandler(PPCPROPERTY_REQUEST PropertyRequest)
{
    ULONG Channel;

    if (!PropertyRequest || PropertyRequest->Node != 0 || !PropertyRequest->MajorTarget)
        return STATUS_INVALID_PARAMETER;
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
        return BthA2dpBasicSupport(PropertyRequest, VT_I4, BTHA2DP_VOLUME_MINIMUM, BTHA2DP_VOLUME_MAXIMUM,
                                   BTHA2DP_VOLUME_STEP);
    if (PropertyRequest->InstanceSize < sizeof(LONG) || PropertyRequest->ValueSize < sizeof(LONG))
    {
        PropertyRequest->ValueSize = sizeof(LONG);
        return STATUS_BUFFER_TOO_SMALL;
    }
    Channel = *static_cast<PULONG>(PropertyRequest->Instance);
    if (Channel >= BTHA2DP_CHANNELS && Channel != ~0UL)
        return STATUS_INVALID_PARAMETER;
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_GET)
    {
        *static_cast<PLONG>(PropertyRequest->Value) = A2dpGetVolume(BthA2dpTopologyContext(PropertyRequest),
                                                                     Channel == ~0UL ? 0 : Channel);
        PropertyRequest->ValueSize = sizeof(LONG);
        return STATUS_SUCCESS;
    }
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_SET)
    {
        LONG Level = *static_cast<PLONG>(PropertyRequest->Value);

        if (Channel == ~0UL)
        {
            A2dpSetVolume(BthA2dpTopologyContext(PropertyRequest), 0, Level);
            A2dpSetVolume(BthA2dpTopologyContext(PropertyRequest), 1, Level);
        }
        else
        {
            A2dpSetVolume(BthA2dpTopologyContext(PropertyRequest), Channel, Level);
        }
        return STATUS_SUCCESS;
    }
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
BthA2dpMuteHandler(PPCPROPERTY_REQUEST PropertyRequest)
{
    if (!PropertyRequest || PropertyRequest->Node != 1 || !PropertyRequest->MajorTarget)
        return STATUS_INVALID_PARAMETER;
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
        return BthA2dpBasicSupport(PropertyRequest, VT_BOOL, FALSE, TRUE, 1);
    if (PropertyRequest->ValueSize < sizeof(BOOL))
    {
        PropertyRequest->ValueSize = sizeof(BOOL);
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_GET)
    {
        *static_cast<PBOOL>(PropertyRequest->Value) = A2dpGetMute(BthA2dpTopologyContext(PropertyRequest));
        PropertyRequest->ValueSize = sizeof(BOOL);
        return STATUS_SUCCESS;
    }
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_SET)
    {
        A2dpSetMute(BthA2dpTopologyContext(PropertyRequest), *static_cast<PBOOL>(PropertyRequest->Value) != FALSE);
        return STATUS_SUCCESS;
    }
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
BthA2dpJackHandler(PPCPROPERTY_REQUEST PropertyRequest)
{
    PKSMULTIPLE_ITEM MultipleItem;
    PKSJACK_DESCRIPTION Jack;
    ULONG PinId, Count, Required;

    if (!PropertyRequest || !PropertyRequest->MajorTarget || !PropertyRequest->Instance ||
        PropertyRequest->InstanceSize < sizeof(ULONG))
        return STATUS_INVALID_PARAMETER;
    PinId = *static_cast<PULONG>(PropertyRequest->Instance);
    if (PinId > 1)
        return STATUS_INVALID_PARAMETER;
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
    {
        if (PropertyRequest->ValueSize < sizeof(ULONG))
        {
            PropertyRequest->ValueSize = sizeof(ULONG);
            return STATUS_BUFFER_TOO_SMALL;
        }
        *static_cast<PULONG>(PropertyRequest->Value) = KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET;
        PropertyRequest->ValueSize = sizeof(ULONG);
        return STATUS_SUCCESS;
    }
    if (!(PropertyRequest->Verb & KSPROPERTY_TYPE_GET))
        return STATUS_NOT_SUPPORTED;
    Count = PinId == 1 ? 1 : 0;
    Required = sizeof(KSMULTIPLE_ITEM) + Count * sizeof(KSJACK_DESCRIPTION);
    if (!PropertyRequest->ValueSize)
    {
        PropertyRequest->ValueSize = Required;
        return STATUS_BUFFER_OVERFLOW;
    }
    if (PropertyRequest->ValueSize < Required)
    {
        PropertyRequest->ValueSize = Required;
        return STATUS_BUFFER_TOO_SMALL;
    }
    MultipleItem = static_cast<PKSMULTIPLE_ITEM>(PropertyRequest->Value);
    MultipleItem->Size = Required;
    MultipleItem->Count = Count;
    PropertyRequest->ValueSize = Required;
    if (!Count)
        return STATUS_SUCCESS;
    Jack = reinterpret_cast<PKSJACK_DESCRIPTION>(MultipleItem + 1);
    RtlZeroMemory(Jack, sizeof(*Jack));
    Jack->ChannelMapping = KSAUDIO_SPEAKER_STEREO;
    Jack->ConnectionType = eConnTypeOtherDigital;
    Jack->GeoLocation = eGeoLocNotApplicable;
    Jack->GenLocation = eGenLocOther;
    Jack->PortConnection = ePortConnUnknown;
    Jack->IsConnected = A2dpIsConnected(BthA2dpTopologyContext(PropertyRequest));
    return STATUS_SUCCESS;
}

static PCPROPERTY_ITEM BthA2dpVolumeProperties[] =
{
    {
        &KSPROPSETID_Audio,
        KSPROPERTY_AUDIO_VOLUMELEVEL,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT,
        BthA2dpVolumeHandler
    }
};

static PCPROPERTY_ITEM BthA2dpMuteProperties[] =
{
    {
        &KSPROPSETID_Audio,
        KSPROPERTY_AUDIO_MUTE,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT,
        BthA2dpMuteHandler
    }
};

static PCPROPERTY_ITEM BthA2dpJackProperties[] =
{
    {
        &KSPROPSETID_Jack,
        KSPROPERTY_JACK_DESCRIPTION,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT,
        BthA2dpJackHandler
    }
};

DEFINE_PCAUTOMATION_TABLE_PROP(BthA2dpVolumeAutomation, BthA2dpVolumeProperties);
DEFINE_PCAUTOMATION_TABLE_PROP(BthA2dpMuteAutomation, BthA2dpMuteProperties);
DEFINE_PCAUTOMATION_TABLE_PROP(BthA2dpJackAutomation, BthA2dpJackProperties);

static PCPIN_DESCRIPTOR BthA2dpWavePins[] =
{
    {
        1, 1, 0, NULL,
        {
            0, NULL, 0, NULL,
            RTL_NUMBER_OF(BthA2dpPcmDataRanges), BthA2dpPcmDataRanges,
            KSPIN_DATAFLOW_IN, KSPIN_COMMUNICATION_SINK, &KSCATEGORY_AUDIO, NULL, {0}
        }
    },
    {
        0, 0, 0, NULL,
        {
            0, NULL, 0, NULL,
            RTL_NUMBER_OF(BthA2dpBridgeDataRanges), BthA2dpBridgeDataRanges,
            KSPIN_DATAFLOW_OUT, KSPIN_COMMUNICATION_NONE, &KSCATEGORY_AUDIO, NULL, {0}
        }
    }
};

static PCNODE_DESCRIPTOR BthA2dpWaveNodes[] =
{
    {0, NULL, &KSNODETYPE_DAC, NULL}
};

static PCCONNECTION_DESCRIPTOR BthA2dpWaveConnections[] =
{
    {PCFILTER_NODE, 0, 0, 1},
    {0, 0, PCFILTER_NODE, 1}
};

PCFILTER_DESCRIPTOR BthA2dpWaveFilterDescriptor =
{
    0,
    NULL,
    sizeof(PCPIN_DESCRIPTOR),
    RTL_NUMBER_OF(BthA2dpWavePins),
    BthA2dpWavePins,
    sizeof(PCNODE_DESCRIPTOR),
    RTL_NUMBER_OF(BthA2dpWaveNodes),
    BthA2dpWaveNodes,
    RTL_NUMBER_OF(BthA2dpWaveConnections),
    BthA2dpWaveConnections,
    0,
    NULL
};

static PCPIN_DESCRIPTOR BthA2dpTopologyPins[] =
{
    {
        0, 0, 0, NULL,
        {
            0, NULL, 0, NULL,
            RTL_NUMBER_OF(BthA2dpBridgeDataRanges), BthA2dpBridgeDataRanges,
            KSPIN_DATAFLOW_IN, KSPIN_COMMUNICATION_NONE, &KSCATEGORY_AUDIO, NULL, {0}
        }
    },
    {
        0, 0, 0, NULL,
        {
            0, NULL, 0, NULL,
            RTL_NUMBER_OF(BthA2dpBridgeDataRanges), BthA2dpBridgeDataRanges,
            KSPIN_DATAFLOW_OUT, KSPIN_COMMUNICATION_NONE, &KSNODETYPE_SPEAKER, NULL, {0}
        }
    }
};

static PCNODE_DESCRIPTOR BthA2dpTopologyNodes[] =
{
    {0, &BthA2dpVolumeAutomation, &KSNODETYPE_VOLUME, &KSAUDFNAME_MASTER_VOLUME},
    {0, &BthA2dpMuteAutomation, &KSNODETYPE_MUTE, &KSAUDFNAME_MASTER_MUTE}
};

static PCCONNECTION_DESCRIPTOR BthA2dpTopologyConnections[] =
{
    {PCFILTER_NODE, 0, 0, 1},
    {0, 0, 1, 1},
    {1, 0, PCFILTER_NODE, 1}
};

PCFILTER_DESCRIPTOR BthA2dpTopologyFilterDescriptor =
{
    0,
    &BthA2dpJackAutomation,
    sizeof(PCPIN_DESCRIPTOR),
    RTL_NUMBER_OF(BthA2dpTopologyPins),
    BthA2dpTopologyPins,
    sizeof(PCNODE_DESCRIPTOR),
    RTL_NUMBER_OF(BthA2dpTopologyNodes),
    BthA2dpTopologyNodes,
    RTL_NUMBER_OF(BthA2dpTopologyConnections),
    BthA2dpTopologyConnections,
    0,
    NULL
};

BOOLEAN
BthA2dpIsFormatSupported(PKSDATAFORMAT DataFormat)
{
    PKSDATAFORMAT_WAVEFORMATEX WaveFormat;

    if (!DataFormat || DataFormat->FormatSize < sizeof(KSDATAFORMAT_WAVEFORMATEX))
        return FALSE;
    if (!IsEqualGUIDAligned(DataFormat->MajorFormat, KSDATAFORMAT_TYPE_AUDIO) ||
        !IsEqualGUIDAligned(DataFormat->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) ||
        !IsEqualGUIDAligned(DataFormat->Specifier, KSDATAFORMAT_SPECIFIER_WAVEFORMATEX))
        return FALSE;
    WaveFormat = reinterpret_cast<PKSDATAFORMAT_WAVEFORMATEX>(DataFormat);
    if (WaveFormat->WaveFormatEx.wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        PWAVEFORMATEXTENSIBLE Extensible = reinterpret_cast<PWAVEFORMATEXTENSIBLE>(&WaveFormat->WaveFormatEx);

        if (DataFormat->FormatSize < sizeof(KSDATAFORMAT) + sizeof(WAVEFORMATEXTENSIBLE) ||
            !IsEqualGUIDAligned(Extensible->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) ||
            Extensible->Samples.wValidBitsPerSample != BTHA2DP_BITS_PER_SAMPLE)
            return FALSE;
    }
    else if (WaveFormat->WaveFormatEx.wFormatTag != WAVE_FORMAT_PCM)
    {
        return FALSE;
    }
    return WaveFormat->WaveFormatEx.nChannels == BTHA2DP_CHANNELS &&
           WaveFormat->WaveFormatEx.nSamplesPerSec == BTHA2DP_SAMPLE_RATE &&
           WaveFormat->WaveFormatEx.wBitsPerSample == BTHA2DP_BITS_PER_SAMPLE &&
           WaveFormat->WaveFormatEx.nBlockAlign == BTHA2DP_BLOCK_ALIGN &&
           WaveFormat->WaveFormatEx.nAvgBytesPerSec == BTHA2DP_SAMPLE_RATE * BTHA2DP_BLOCK_ALIGN;
}

CBthA2dpTopology::CBthA2dpTopology(CBthA2dpAdapter *Adapter) : m_Adapter(Adapter)
{
    m_Adapter->AddRef();
}

CBthA2dpTopology::~CBthA2dpTopology()
{
    m_Adapter->Release();
}

NTSTATUS
NTAPI
CBthA2dpTopology::QueryInterface(REFIID InterfaceId, PVOID *Interface)
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
CBthA2dpTopology::GetDescription(PPCFILTER_DESCRIPTOR *Description)
{
    if (!Description)
        return STATUS_INVALID_PARAMETER;
    *Description = &BthA2dpTopologyFilterDescriptor;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CBthA2dpTopology::DataRangeIntersection(
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
CBthA2dpTopology::Init(PUNKNOWN UnknownAdapter, PRESOURCELIST ResourceList, PPORTTOPOLOGY Port)
{
    UNREFERENCED_PARAMETER(UnknownAdapter);
    UNREFERENCED_PARAMETER(ResourceList);
    UNREFERENCED_PARAMETER(Port);
    return STATUS_SUCCESS;
}

CBthA2dpWave::CBthA2dpWave(CBthA2dpAdapter *Adapter) : m_Adapter(Adapter)
{
    m_Adapter->AddRef();
}

CBthA2dpWave::~CBthA2dpWave()
{
    m_Adapter->Release();
}

NTSTATUS
NTAPI
CBthA2dpWave::QueryInterface(REFIID InterfaceId, PVOID *Interface)
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
CBthA2dpWave::GetDescription(PPCFILTER_DESCRIPTOR *Description)
{
    if (!Description)
        return STATUS_INVALID_PARAMETER;
    *Description = &BthA2dpWaveFilterDescriptor;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CBthA2dpWave::DataRangeIntersection(
    ULONG PinId,
    PKSDATARANGE DataRange,
    PKSDATARANGE MatchingDataRange,
    ULONG OutputBufferLength,
    PVOID ResultantFormat,
    PULONG ResultantFormatLength)
{
    PKSDATAFORMAT_WAVEFORMATEX Format;

    UNREFERENCED_PARAMETER(MatchingDataRange);
    if (!ResultantFormatLength || PinId != 0 || !DataRange)
        return STATUS_INVALID_PARAMETER;
    if (!IsEqualGUIDAligned(DataRange->MajorFormat, KSDATAFORMAT_TYPE_AUDIO) ||
        !IsEqualGUIDAligned(DataRange->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) ||
        !IsEqualGUIDAligned(DataRange->Specifier, KSDATAFORMAT_SPECIFIER_WAVEFORMATEX))
        return STATUS_NO_MATCH;
    *ResultantFormatLength = sizeof(KSDATAFORMAT_WAVEFORMATEX);
    if (!OutputBufferLength || !ResultantFormat)
        return STATUS_BUFFER_OVERFLOW;
    if (OutputBufferLength < sizeof(KSDATAFORMAT_WAVEFORMATEX))
        return STATUS_BUFFER_TOO_SMALL;
    Format = static_cast<PKSDATAFORMAT_WAVEFORMATEX>(ResultantFormat);
    RtlZeroMemory(Format, sizeof(*Format));
    Format->DataFormat.FormatSize = sizeof(KSDATAFORMAT_WAVEFORMATEX);
    Format->DataFormat.SampleSize = BTHA2DP_BLOCK_ALIGN;
    Format->DataFormat.MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
    Format->DataFormat.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    Format->DataFormat.Specifier = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;
    Format->WaveFormatEx.wFormatTag = WAVE_FORMAT_PCM;
    Format->WaveFormatEx.nChannels = BTHA2DP_CHANNELS;
    Format->WaveFormatEx.nSamplesPerSec = BTHA2DP_SAMPLE_RATE;
    Format->WaveFormatEx.nAvgBytesPerSec = BTHA2DP_SAMPLE_RATE * BTHA2DP_BLOCK_ALIGN;
    Format->WaveFormatEx.nBlockAlign = BTHA2DP_BLOCK_ALIGN;
    Format->WaveFormatEx.wBitsPerSample = BTHA2DP_BITS_PER_SAMPLE;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CBthA2dpWave::Init(PUNKNOWN UnknownAdapter, PRESOURCELIST ResourceList, PPORTWAVERT Port)
{
    UNREFERENCED_PARAMETER(UnknownAdapter);
    UNREFERENCED_PARAMETER(ResourceList);
    UNREFERENCED_PARAMETER(Port);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CBthA2dpWave::NewStream(
    PMINIPORTWAVERTSTREAM *Stream,
    PPORTWAVERTSTREAM PortStream,
    ULONG Pin,
    BOOLEAN Capture,
    PKSDATAFORMAT DataFormat)
{
    CBthA2dpStream *NewStream;

    if (!Stream || !PortStream || Pin != 0 || Capture || !BthA2dpIsFormatSupported(DataFormat))
        return STATUS_NOT_SUPPORTED;
    if (!m_Adapter->ClaimStream())
        return STATUS_DEVICE_BUSY;
    NewStream = new (NonPagedPool, BTHA2DP_TAG) CBthA2dpStream(m_Adapter, PortStream);
    if (!NewStream)
    {
        m_Adapter->ReleaseStream();
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    NewStream->AddRef();
    *Stream = static_cast<PMINIPORTWAVERTSTREAM>(NewStream);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CBthA2dpWave::GetDeviceDescription(PDEVICE_DESCRIPTION DeviceDescription)
{
    if (!DeviceDescription)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(DeviceDescription, sizeof(*DeviceDescription));
    DeviceDescription->Version = DEVICE_DESCRIPTION_VERSION1;
    DeviceDescription->Master = TRUE;
    DeviceDescription->Dma64BitAddresses = TRUE;
    DeviceDescription->InterfaceType = Internal;
    DeviceDescription->MaximumLength = BTHA2DP_MAX_BUFFER_SIZE;
    return STATUS_SUCCESS;
}
