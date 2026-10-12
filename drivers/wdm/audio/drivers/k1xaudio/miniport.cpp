/*
 * PROJECT:         LiberNT SpacemiT K1 Audio Driver
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         WaveRT and topology miniports
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

static const ULONG K1xAudioTopologyPinCount = 4;

static KSDATARANGE_AUDIO K1xAudioPcmDataRange =
{
    {{
        sizeof(KSDATARANGE_AUDIO),
        0,
        K1XAUDIO_BLOCK_ALIGN,
        0,
        {STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO)},
        {STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM)},
        {STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)}
    }},
    K1XAUDIO_CHANNELS,
    K1XAUDIO_BITS_PER_SAMPLE,
    K1XAUDIO_BITS_PER_SAMPLE,
    K1XAUDIO_SAMPLE_RATE,
    K1XAUDIO_SAMPLE_RATE
};

static PKSDATARANGE K1xAudioPcmDataRanges[] =
{
    reinterpret_cast<PKSDATARANGE>(&K1xAudioPcmDataRange)
};

static KSDATARANGE K1xAudioBridgeDataRange =
{{
    sizeof(KSDATARANGE),
    0,
    0,
    0,
    {STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO)},
    {STATICGUIDOF(KSDATAFORMAT_SUBTYPE_ANALOG)},
    {STATICGUIDOF(KSDATAFORMAT_SPECIFIER_NONE)}
}};

static PKSDATARANGE K1xAudioBridgeDataRanges[] =
{
    &K1xAudioBridgeDataRange
};

static NTSTATUS
K1xAudioBasicSupport(
    PPCPROPERTY_REQUEST PropertyRequest,
    ULONG AccessFlags,
    ULONG PropertyType,
    ULONG DescriptionSize,
    ULONG MembersListCount)
{
    PKSPROPERTY_DESCRIPTION Description;
    ULONG ValueSize = PropertyRequest->ValueSize;

    if (ValueSize < sizeof(ULONG))
    {
        PropertyRequest->ValueSize = sizeof(ULONG);
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (ValueSize < sizeof(KSPROPERTY_DESCRIPTION))
    {
        *static_cast<PULONG>(PropertyRequest->Value) = AccessFlags;
        PropertyRequest->ValueSize = sizeof(ULONG);
        return STATUS_SUCCESS;
    }

    Description = static_cast<PKSPROPERTY_DESCRIPTION>(PropertyRequest->Value);
    RtlZeroMemory(Description, sizeof(*Description));
    Description->AccessFlags = AccessFlags;
    Description->DescriptionSize = DescriptionSize;
    if (PropertyType == VT_ILLEGAL)
    {
        Description->PropTypeSet.Set = GUID_NULL;
        Description->PropTypeSet.Id = 0;
    }
    else
    {
        Description->PropTypeSet.Set = KSPROPTYPESETID_General;
        Description->PropTypeSet.Id = PropertyType;
    }
    Description->MembersListCount = MembersListCount;
    PropertyRequest->ValueSize = sizeof(KSPROPERTY_DESCRIPTION);

    return ValueSize < DescriptionSize ? STATUS_SUCCESS : STATUS_MORE_ENTRIES;
}

static NTSTATUS NTAPI
K1xAudioVolumePropertyHandler(PPCPROPERTY_REQUEST PropertyRequest)
{
    const ULONG DescriptionSize = sizeof(KSPROPERTY_DESCRIPTION) +
                                  sizeof(KSPROPERTY_MEMBERSHEADER) +
                                  K1XAUDIO_CHANNELS * sizeof(KSPROPERTY_STEPPING_LONG);
    CK1xAudioTopology *Topology;
    CK1xAudioAdapter *Adapter;
    ULONG Channel;

    if (!PropertyRequest || PropertyRequest->Node != 0 || !PropertyRequest->MajorTarget)
        return STATUS_INVALID_PARAMETER;

    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
    {
        NTSTATUS Status = K1xAudioBasicSupport(
            PropertyRequest,
            KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET,
            VT_I4,
            DescriptionSize,
            1);

        if (Status != STATUS_MORE_ENTRIES)
            return Status;

        PKSPROPERTY_DESCRIPTION Description =
            static_cast<PKSPROPERTY_DESCRIPTION>(PropertyRequest->Value);

        PKSPROPERTY_MEMBERSHEADER Members =
            reinterpret_cast<PKSPROPERTY_MEMBERSHEADER>(Description + 1);
        Members->MembersFlags = KSPROPERTY_MEMBER_STEPPEDRANGES;
        Members->MembersSize = sizeof(KSPROPERTY_STEPPING_LONG);
        Members->MembersCount = K1XAUDIO_CHANNELS;
        Members->Flags = KSPROPERTY_MEMBER_FLAG_BASICSUPPORT_MULTICHANNEL;

        PKSPROPERTY_STEPPING_LONG Range =
            reinterpret_cast<PKSPROPERTY_STEPPING_LONG>(Members + 1);
        for (ULONG Index = 0; Index < K1XAUDIO_CHANNELS; ++Index)
        {
            Range[Index].SteppingDelta = K1XAUDIO_VOLUME_STEP;
            Range[Index].Bounds.SignedMinimum = K1XAUDIO_VOLUME_MINIMUM;
            Range[Index].Bounds.SignedMaximum = K1XAUDIO_VOLUME_MAXIMUM;
        }
        PropertyRequest->ValueSize = DescriptionSize;
        return STATUS_SUCCESS;
    }

    if (PropertyRequest->InstanceSize < sizeof(LONG) ||
        PropertyRequest->ValueSize < sizeof(LONG))
    {
        PropertyRequest->ValueSize = sizeof(LONG);
        return STATUS_BUFFER_TOO_SMALL;
    }

    Channel = *static_cast<PULONG>(PropertyRequest->Instance);
    Topology = static_cast<CK1xAudioTopology *>(
        static_cast<PMINIPORTTOPOLOGY>(PropertyRequest->MajorTarget));
    Adapter = Topology->GetAdapter();

    if (PropertyRequest->Verb & KSPROPERTY_TYPE_GET)
    {
        NTSTATUS Status = Adapter->GetVolume(Channel, static_cast<PLONG>(PropertyRequest->Value));
        if (NT_SUCCESS(Status))
            PropertyRequest->ValueSize = sizeof(LONG);
        return Status;
    }
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_SET)
        return Adapter->SetVolume(Channel, *static_cast<PLONG>(PropertyRequest->Value));

    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS NTAPI
K1xAudioMutePropertyHandler(PPCPROPERTY_REQUEST PropertyRequest)
{
    const ULONG DescriptionSize = sizeof(KSPROPERTY_DESCRIPTION) +
                                  sizeof(KSPROPERTY_MEMBERSHEADER) +
                                  K1XAUDIO_CHANNELS * sizeof(KSPROPERTY_STEPPING_LONG);
    CK1xAudioTopology *Topology;
    CK1xAudioAdapter *Adapter;

    if (!PropertyRequest || PropertyRequest->Node != 1 || !PropertyRequest->MajorTarget)
        return STATUS_INVALID_PARAMETER;

    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
    {
        NTSTATUS Status = K1xAudioBasicSupport(
            PropertyRequest,
            KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET,
            VT_BOOL,
            DescriptionSize,
            1);

        if (Status != STATUS_MORE_ENTRIES)
            return Status;

        PKSPROPERTY_DESCRIPTION Description =
            static_cast<PKSPROPERTY_DESCRIPTION>(PropertyRequest->Value);
        PKSPROPERTY_MEMBERSHEADER Members =
            reinterpret_cast<PKSPROPERTY_MEMBERSHEADER>(Description + 1);
        Members->MembersFlags = KSPROPERTY_MEMBER_STEPPEDRANGES;
        Members->MembersSize = sizeof(KSPROPERTY_STEPPING_LONG);
        Members->MembersCount = K1XAUDIO_CHANNELS;
        Members->Flags = KSPROPERTY_MEMBER_FLAG_BASICSUPPORT_MULTICHANNEL |
                         KSPROPERTY_MEMBER_FLAG_BASICSUPPORT_UNIFORM;

        PKSPROPERTY_STEPPING_LONG Range =
            reinterpret_cast<PKSPROPERTY_STEPPING_LONG>(Members + 1);
        for (ULONG Index = 0; Index < K1XAUDIO_CHANNELS; ++Index)
        {
            Range[Index].SteppingDelta = 1;
            Range[Index].Bounds.SignedMinimum = FALSE;
            Range[Index].Bounds.SignedMaximum = TRUE;
        }

        PropertyRequest->ValueSize = DescriptionSize;
        return STATUS_SUCCESS;
    }

    if (PropertyRequest->ValueSize < sizeof(BOOL))
    {
        PropertyRequest->ValueSize = sizeof(BOOL);
        return STATUS_BUFFER_TOO_SMALL;
    }

    Topology = static_cast<CK1xAudioTopology *>(
        static_cast<PMINIPORTTOPOLOGY>(PropertyRequest->MajorTarget));
    Adapter = Topology->GetAdapter();

    if (PropertyRequest->Verb & KSPROPERTY_TYPE_GET)
    {
        *static_cast<PBOOL>(PropertyRequest->Value) = Adapter->GetMute();
        PropertyRequest->ValueSize = sizeof(BOOL);
        return STATUS_SUCCESS;
    }
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_SET)
    {
        Adapter->SetMute(*static_cast<PBOOL>(PropertyRequest->Value));
        return STATUS_SUCCESS;
    }

    return STATUS_NOT_SUPPORTED;
}

template <typename Description>
static NTSTATUS
K1xAudioPrepareJackDescription(
    PPCPROPERTY_REQUEST PropertyRequest,
    ULONG PinId,
    PKSMULTIPLE_ITEM *MultipleItem,
    Description **JackDescription)
{
    ULONG DescriptionCount;
    ULONG RequiredSize;

    if (PinId >= K1xAudioTopologyPinCount)
        return STATUS_INVALID_PARAMETER;

    DescriptionCount = PinId == 1 ? 1 : 0;
    RequiredSize = sizeof(KSMULTIPLE_ITEM) +
                   DescriptionCount * sizeof(Description);

    if (!PropertyRequest->ValueSize)
    {
        PropertyRequest->ValueSize = RequiredSize;
        return STATUS_BUFFER_OVERFLOW;
    }

    if (PropertyRequest->ValueSize < RequiredSize)
    {
        PropertyRequest->ValueSize = RequiredSize;
        return STATUS_BUFFER_TOO_SMALL;
    }

    *MultipleItem = static_cast<PKSMULTIPLE_ITEM>(PropertyRequest->Value);
    (*MultipleItem)->Size = RequiredSize;
    (*MultipleItem)->Count = DescriptionCount;
    *JackDescription = DescriptionCount
        ? reinterpret_cast<Description *>(*MultipleItem + 1)
        : NULL;
    PropertyRequest->ValueSize = RequiredSize;
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI
K1xAudioJackDescriptionPropertyHandler(PPCPROPERTY_REQUEST PropertyRequest)
{
    CK1xAudioTopology *Topology;
    PKSMULTIPLE_ITEM MultipleItem;
    PKSJACK_DESCRIPTION JackDescription;
    ULONG PinId;
    NTSTATUS Status;

    if (!PropertyRequest || !PropertyRequest->MajorTarget ||
        !PropertyRequest->Instance ||
        PropertyRequest->InstanceSize < sizeof(ULONG))
    {
        return STATUS_INVALID_PARAMETER;
    }

    PinId = *static_cast<PULONG>(PropertyRequest->Instance);
    if (PinId >= K1xAudioTopologyPinCount)
        return STATUS_INVALID_PARAMETER;
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
    {
        return K1xAudioBasicSupport(
            PropertyRequest,
            KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET,
            VT_ILLEGAL,
            sizeof(KSPROPERTY_DESCRIPTION),
            0);
    }
    if (!(PropertyRequest->Verb & KSPROPERTY_TYPE_GET))
        return STATUS_NOT_SUPPORTED;

    Status = K1xAudioPrepareJackDescription(
        PropertyRequest, PinId, &MultipleItem, &JackDescription);
    if (!NT_SUCCESS(Status) || !JackDescription)
        return Status;

    Topology = static_cast<CK1xAudioTopology *>(
        static_cast<PMINIPORTTOPOLOGY>(PropertyRequest->MajorTarget));
    RtlZeroMemory(JackDescription, sizeof(*JackDescription));
    JackDescription->ConnectionType = eConnType3Point5mm;
    JackDescription->GeoLocation = eGeoLocRear;
    JackDescription->GenLocation = eGenLocPrimaryBox;
    JackDescription->PortConnection = ePortConnJack;
    JackDescription->IsConnected = Topology->GetAdapter()->IsSinkConnected();
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI
K1xAudioJackDescription2PropertyHandler(PPCPROPERTY_REQUEST PropertyRequest)
{
    PKSMULTIPLE_ITEM MultipleItem;
    PKSJACK_DESCRIPTION2 JackDescription;
    ULONG PinId;
    NTSTATUS Status;

    if (!PropertyRequest || !PropertyRequest->MajorTarget ||
        !PropertyRequest->Instance ||
        PropertyRequest->InstanceSize < sizeof(ULONG))
    {
        return STATUS_INVALID_PARAMETER;
    }

    PinId = *static_cast<PULONG>(PropertyRequest->Instance);
    if (PinId >= K1xAudioTopologyPinCount)
        return STATUS_INVALID_PARAMETER;
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
    {
        return K1xAudioBasicSupport(
            PropertyRequest,
            KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET,
            VT_ILLEGAL,
            sizeof(KSPROPERTY_DESCRIPTION),
            0);
    }
    if (!(PropertyRequest->Verb & KSPROPERTY_TYPE_GET))
        return STATUS_NOT_SUPPORTED;

    Status = K1xAudioPrepareJackDescription(
        PropertyRequest, PinId, &MultipleItem, &JackDescription);
    if (!NT_SUCCESS(Status) || !JackDescription)
        return Status;

    RtlZeroMemory(JackDescription, sizeof(*JackDescription));
    JackDescription->JackCapabilities =
        JACKDESC2_PRESENCE_DETECT_CAPABILITY;
    return STATUS_SUCCESS;
}

static PCPROPERTY_ITEM K1xAudioVolumeProperties[] =
{
    {
        &KSPROPSETID_Audio,
        KSPROPERTY_AUDIO_VOLUMELEVEL,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT,
        K1xAudioVolumePropertyHandler
    }
};

static PCPROPERTY_ITEM K1xAudioMuteProperties[] =
{
    {
        &KSPROPSETID_Audio,
        KSPROPERTY_AUDIO_MUTE,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT,
        K1xAudioMutePropertyHandler
    }
};

static PCPROPERTY_ITEM K1xAudioJackProperties[] =
{
    {
        &KSPROPSETID_Jack,
        KSPROPERTY_JACK_DESCRIPTION,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT,
        K1xAudioJackDescriptionPropertyHandler
    },
    {
        &KSPROPSETID_Jack,
        KSPROPERTY_JACK_DESCRIPTION2,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT,
        K1xAudioJackDescription2PropertyHandler
    }
};

static NTSTATUS NTAPI
K1xAudioJackEventHandler(PPCEVENT_REQUEST EventRequest)
{
    CK1xAudioTopology *Topology;

    if (!EventRequest || !EventRequest->MajorTarget)
        return STATUS_INVALID_PARAMETER;

    switch (EventRequest->Verb)
    {
        case PCEVENT_VERB_ADD:
            if (!EventRequest->EventEntry)
                return STATUS_INVALID_PARAMETER;
            Topology = static_cast<CK1xAudioTopology *>(
                static_cast<PMINIPORTTOPOLOGY>(EventRequest->MajorTarget));
            Topology->AddEventToEventList(EventRequest->EventEntry);
            return STATUS_SUCCESS;

        case PCEVENT_VERB_REMOVE:
        case PCEVENT_VERB_SUPPORT:
            return STATUS_SUCCESS;

        default:
            return STATUS_INVALID_PARAMETER;
    }
}

static PCEVENT_ITEM K1xAudioJackEvents[] =
{
    {
        &KSEVENTSETID_PinCapsChange,
        KSEVENT_PINCAPS_JACKINFOCHANGE,
        PCEVENT_ITEM_FLAG_ENABLE | PCEVENT_ITEM_FLAG_BASICSUPPORT,
        K1xAudioJackEventHandler
    }
};

DEFINE_PCAUTOMATION_TABLE_PROP(K1xAudioVolumeAutomation, K1xAudioVolumeProperties);
DEFINE_PCAUTOMATION_TABLE_PROP(K1xAudioMuteAutomation, K1xAudioMuteProperties);
DEFINE_PCAUTOMATION_TABLE_PROP_EVENT(
    K1xAudioJackAutomation,
    K1xAudioJackProperties,
    K1xAudioJackEvents);

static PCPIN_DESCRIPTOR K1xAudioWavePins[] =
{
    {
        1,
        1,
        0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            RTL_NUMBER_OF(K1xAudioPcmDataRanges),
            K1xAudioPcmDataRanges,
            KSPIN_DATAFLOW_IN,
            KSPIN_COMMUNICATION_SINK,
            &KSCATEGORY_AUDIO,
            NULL,
            {0}
        }
    },
    {
        0,
        0,
        0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            RTL_NUMBER_OF(K1xAudioBridgeDataRanges),
            K1xAudioBridgeDataRanges,
            KSPIN_DATAFLOW_OUT,
            KSPIN_COMMUNICATION_NONE,
            &KSCATEGORY_AUDIO,
            NULL,
            {0}
        }
    },
    {
        1,
        1,
        0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            RTL_NUMBER_OF(K1xAudioPcmDataRanges),
            K1xAudioPcmDataRanges,
            KSPIN_DATAFLOW_OUT,
            KSPIN_COMMUNICATION_SINK,
            &KSCATEGORY_AUDIO,
            NULL,
            {0}
        }
    },
    {
        0,
        0,
        0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            RTL_NUMBER_OF(K1xAudioBridgeDataRanges),
            K1xAudioBridgeDataRanges,
            KSPIN_DATAFLOW_IN,
            KSPIN_COMMUNICATION_NONE,
            &KSCATEGORY_AUDIO,
            NULL,
            {0}
        }
    }
};

static PCNODE_DESCRIPTOR K1xAudioWaveNodes[] =
{
    {0, NULL, &KSNODETYPE_DAC, NULL},
    {0, NULL, &KSNODETYPE_ADC, NULL}
};

static PCCONNECTION_DESCRIPTOR K1xAudioWaveConnections[] =
{
    {PCFILTER_NODE, 0, 0, 1},
    {0, 0, PCFILTER_NODE, 1},
    {PCFILTER_NODE, 3, 1, 1},
    {1, 0, PCFILTER_NODE, 2}
};

PCFILTER_DESCRIPTOR K1xAudioWaveFilterDescriptor =
{
    0,
    NULL,
    sizeof(PCPIN_DESCRIPTOR),
    RTL_NUMBER_OF(K1xAudioWavePins),
    K1xAudioWavePins,
    sizeof(PCNODE_DESCRIPTOR),
    RTL_NUMBER_OF(K1xAudioWaveNodes),
    K1xAudioWaveNodes,
    RTL_NUMBER_OF(K1xAudioWaveConnections),
    K1xAudioWaveConnections,
    0,
    NULL
};

static PCPIN_DESCRIPTOR K1xAudioTopologyPins[] =
{
    {
        0,
        0,
        0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            RTL_NUMBER_OF(K1xAudioBridgeDataRanges),
            K1xAudioBridgeDataRanges,
            KSPIN_DATAFLOW_IN,
            KSPIN_COMMUNICATION_NONE,
            &KSCATEGORY_AUDIO,
            NULL,
            {0}
        }
    },
    {
        0,
        0,
        0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            RTL_NUMBER_OF(K1xAudioBridgeDataRanges),
            K1xAudioBridgeDataRanges,
            KSPIN_DATAFLOW_OUT,
            KSPIN_COMMUNICATION_NONE,
            &KSNODETYPE_HEADPHONES,
            NULL,
            {0}
        }
    },
    {
        0,
        0,
        0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            RTL_NUMBER_OF(K1xAudioBridgeDataRanges),
            K1xAudioBridgeDataRanges,
            KSPIN_DATAFLOW_IN,
            KSPIN_COMMUNICATION_NONE,
            &KSNODETYPE_MICROPHONE,
            NULL,
            {0}
        }
    },
    {
        0,
        0,
        0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            RTL_NUMBER_OF(K1xAudioBridgeDataRanges),
            K1xAudioBridgeDataRanges,
            KSPIN_DATAFLOW_OUT,
            KSPIN_COMMUNICATION_NONE,
            &KSCATEGORY_AUDIO,
            NULL,
            {0}
        }
    }
};

static PCCONNECTION_DESCRIPTOR K1xAudioTopologyConnections[] =
{
    {PCFILTER_NODE, 0, 0, 1},
    {0, 0, 1, 1},
    {1, 0, PCFILTER_NODE, 1},
    {PCFILTER_NODE, 2, PCFILTER_NODE, 3}
};

static PCNODE_DESCRIPTOR K1xAudioTopologyNodes[] =
{
    {0, &K1xAudioVolumeAutomation, &KSNODETYPE_VOLUME, &KSAUDFNAME_MASTER_VOLUME},
    {0, &K1xAudioMuteAutomation, &KSNODETYPE_MUTE, &KSAUDFNAME_MASTER_MUTE}
};

PCFILTER_DESCRIPTOR K1xAudioTopologyFilterDescriptor =
{
    0,
    &K1xAudioJackAutomation,
    sizeof(PCPIN_DESCRIPTOR),
    RTL_NUMBER_OF(K1xAudioTopologyPins),
    K1xAudioTopologyPins,
    sizeof(PCNODE_DESCRIPTOR),
    RTL_NUMBER_OF(K1xAudioTopologyNodes),
    K1xAudioTopologyNodes,
    RTL_NUMBER_OF(K1xAudioTopologyConnections),
    K1xAudioTopologyConnections,
    0,
    NULL
};

BOOLEAN
K1xAudioIsFormatSupported(PKSDATAFORMAT DataFormat)
{
    PKSDATAFORMAT_WAVEFORMATEX WaveFormat;
    PWAVEFORMATEXTENSIBLE Extensible;

    if (!DataFormat || DataFormat->FormatSize < sizeof(KSDATAFORMAT_WAVEFORMATEX))
        return FALSE;

    if (!IsEqualGUIDAligned(DataFormat->MajorFormat, KSDATAFORMAT_TYPE_AUDIO) ||
        !IsEqualGUIDAligned(DataFormat->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) ||
        !IsEqualGUIDAligned(DataFormat->Specifier, KSDATAFORMAT_SPECIFIER_WAVEFORMATEX))
    {
        return FALSE;
    }

    WaveFormat = reinterpret_cast<PKSDATAFORMAT_WAVEFORMATEX>(DataFormat);
    if (WaveFormat->WaveFormatEx.wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        if (DataFormat->FormatSize !=
                sizeof(KSDATAFORMAT) + sizeof(WAVEFORMATEXTENSIBLE) ||
            WaveFormat->WaveFormatEx.cbSize !=
                sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
        {
            return FALSE;
        }

        Extensible = reinterpret_cast<PWAVEFORMATEXTENSIBLE>(
            &WaveFormat->WaveFormatEx);
        if (!IsEqualGUIDAligned(Extensible->SubFormat,
                                KSDATAFORMAT_SUBTYPE_PCM) ||
            Extensible->Samples.wValidBitsPerSample !=
                K1XAUDIO_BITS_PER_SAMPLE)
        {
            return FALSE;
        }
    }
    else if (WaveFormat->WaveFormatEx.wFormatTag != WAVE_FORMAT_PCM ||
             DataFormat->FormatSize != sizeof(KSDATAFORMAT_WAVEFORMATEX) ||
             WaveFormat->WaveFormatEx.cbSize)
    {
        return FALSE;
    }

    return WaveFormat->WaveFormatEx.nChannels == K1XAUDIO_CHANNELS &&
           WaveFormat->WaveFormatEx.nSamplesPerSec == K1XAUDIO_SAMPLE_RATE &&
           WaveFormat->WaveFormatEx.nAvgBytesPerSec ==
               K1XAUDIO_SAMPLE_RATE * K1XAUDIO_BLOCK_ALIGN &&
           WaveFormat->WaveFormatEx.wBitsPerSample == K1XAUDIO_BITS_PER_SAMPLE &&
           WaveFormat->WaveFormatEx.nBlockAlign == K1XAUDIO_BLOCK_ALIGN &&
           DataFormat->SampleSize == K1XAUDIO_BLOCK_ALIGN;
}

CK1xAudioTopology::CK1xAudioTopology(CK1xAudioAdapter *Adapter)
    : m_Adapter(Adapter), m_PortEvents(NULL)
{
    m_Adapter->AddRef();
}

CK1xAudioTopology::~CK1xAudioTopology()
{
    if (m_PortEvents)
    {
        m_PortEvents->Release();
        m_PortEvents = NULL;
    }
    m_Adapter->Release();
}

NTSTATUS
NTAPI
CK1xAudioTopology::QueryInterface(REFIID InterfaceId, PVOID *Interface)
{
    if (!Interface)
        return STATUS_INVALID_PARAMETER;

    if (IsEqualGUIDAligned(InterfaceId, IID_IUnknown) ||
        IsEqualGUIDAligned(InterfaceId, IID_IMiniport) ||
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
CK1xAudioTopology::GetDescription(PPCFILTER_DESCRIPTOR *Description)
{
    if (!Description)
        return STATUS_INVALID_PARAMETER;
    *Description = &K1xAudioTopologyFilterDescriptor;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CK1xAudioTopology::DataRangeIntersection(
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
CK1xAudioTopology::Init(PUNKNOWN UnknownAdapter, PRESOURCELIST ResourceList, PPORTTOPOLOGY Port)
{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(UnknownAdapter);
    UNREFERENCED_PARAMETER(ResourceList);

    if (!Port)
        return STATUS_INVALID_PARAMETER;

    Status = Port->QueryInterface(
        IID_IPortEvents,
        reinterpret_cast<PVOID *>(&m_PortEvents));
    if (!NT_SUCCESS(Status))
        return Status;

    Status = m_Adapter->RegisterJackEventPort(m_PortEvents);
    if (!NT_SUCCESS(Status))
    {
        m_PortEvents->Release();
        m_PortEvents = NULL;
    }
    return Status;
}

VOID
CK1xAudioTopology::AddEventToEventList(PKSEVENT_ENTRY EventEntry)
{
    if (m_PortEvents && EventEntry)
        m_PortEvents->AddEventToEventList(EventEntry);
}

CK1xAudioWave::CK1xAudioWave(CK1xAudioAdapter *Adapter) : m_Adapter(Adapter)
{
    m_Adapter->AddRef();
}

CK1xAudioWave::~CK1xAudioWave()
{
    m_Adapter->Release();
}

NTSTATUS
NTAPI
CK1xAudioWave::QueryInterface(REFIID InterfaceId, PVOID *Interface)
{
    if (!Interface)
        return STATUS_INVALID_PARAMETER;

    if (IsEqualGUIDAligned(InterfaceId, IID_IUnknown) ||
        IsEqualGUIDAligned(InterfaceId, IID_IMiniport) ||
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
CK1xAudioWave::GetDescription(PPCFILTER_DESCRIPTOR *Description)
{
    if (!Description)
        return STATUS_INVALID_PARAMETER;
    *Description = &K1xAudioWaveFilterDescriptor;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CK1xAudioWave::DataRangeIntersection(
    ULONG PinId,
    PKSDATARANGE DataRange,
    PKSDATARANGE MatchingDataRange,
    ULONG OutputBufferLength,
    PVOID ResultantFormat,
    PULONG ResultantFormatLength)
{
    if (!ResultantFormatLength || (PinId != 0 && PinId != 2) || !DataRange || !MatchingDataRange)
        return STATUS_INVALID_PARAMETER;

    if (!IsEqualGUIDAligned(DataRange->MajorFormat, KSDATAFORMAT_TYPE_AUDIO) ||
        !IsEqualGUIDAligned(DataRange->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) ||
        !IsEqualGUIDAligned(DataRange->Specifier, KSDATAFORMAT_SPECIFIER_WAVEFORMATEX) ||
        !IsEqualGUIDAligned(MatchingDataRange->MajorFormat, KSDATAFORMAT_TYPE_AUDIO) ||
        !IsEqualGUIDAligned(MatchingDataRange->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) ||
        !IsEqualGUIDAligned(MatchingDataRange->Specifier, KSDATAFORMAT_SPECIFIER_WAVEFORMATEX))
    {
        return STATUS_NO_MATCH;
    }

    if (!K1xAudioIsFormatSupported(reinterpret_cast<PKSDATAFORMAT>(DataRange)))
        return STATUS_NO_MATCH;

    *ResultantFormatLength = DataRange->FormatSize;
    if (!OutputBufferLength || !ResultantFormat)
        return STATUS_BUFFER_OVERFLOW;
    if (OutputBufferLength < DataRange->FormatSize)
        return STATUS_BUFFER_TOO_SMALL;

    RtlCopyMemory(ResultantFormat, DataRange, DataRange->FormatSize);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CK1xAudioWave::Init(PUNKNOWN UnknownAdapter, PRESOURCELIST ResourceList, PPORTWAVERT Port)
{
    UNREFERENCED_PARAMETER(UnknownAdapter);
    UNREFERENCED_PARAMETER(ResourceList);
    UNREFERENCED_PARAMETER(Port);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CK1xAudioWave::NewStream(
    PMINIPORTWAVERTSTREAM *Stream,
    PPORTWAVERTSTREAM PortStream,
    ULONG Pin,
    BOOLEAN Capture,
    PKSDATAFORMAT DataFormat)
{
    CK1xAudioStream *NewStream;

    if (!Stream || !PortStream || Pin != (Capture ? 2u : 0u) || !K1xAudioIsFormatSupported(DataFormat))
        return STATUS_NOT_SUPPORTED;
    if (!m_Adapter->ClaimStream(Capture))
        return STATUS_DEVICE_BUSY;

    NewStream = new (NonPagedPool, TAG_K1XAUDIO) CK1xAudioStream(m_Adapter, PortStream, Capture);
    if (!NewStream)
    {
        m_Adapter->ReleaseStream(Capture);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    NewStream->AddRef();
    *Stream = static_cast<PMINIPORTWAVERTSTREAM>(NewStream);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CK1xAudioWave::GetDeviceDescription(PDEVICE_DESCRIPTION DeviceDescription)
{
    if (!DeviceDescription)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(DeviceDescription, sizeof(*DeviceDescription));
    DeviceDescription->Version = DEVICE_DESCRIPTION_VERSION1;
    DeviceDescription->Master = TRUE;
    DeviceDescription->ScatterGather = FALSE;
    DeviceDescription->Dma32BitAddresses = TRUE;
    DeviceDescription->InterfaceType = Internal;
    DeviceDescription->MaximumLength = K1XAUDIO_MAX_BUFFER_SIZE;
    return STATUS_SUCCESS;
}

NTSTATUS
K1xAudioCreateTopology(PUNKNOWN *Unknown, CK1xAudioAdapter *Adapter)
{
    CK1xAudioTopology *Miniport;

    if (!Unknown || !Adapter)
        return STATUS_INVALID_PARAMETER;
    Miniport = new (NonPagedPool, TAG_K1XAUDIO) CK1xAudioTopology(Adapter);
    if (!Miniport)
        return STATUS_INSUFFICIENT_RESOURCES;
    Miniport->AddRef();
    *Unknown = static_cast<PMINIPORTTOPOLOGY>(Miniport);
    return STATUS_SUCCESS;
}

NTSTATUS
K1xAudioCreateWave(PUNKNOWN *Unknown, CK1xAudioAdapter *Adapter)
{
    CK1xAudioWave *Miniport;

    if (!Unknown || !Adapter)
        return STATUS_INVALID_PARAMETER;
    Miniport = new (NonPagedPool, TAG_K1XAUDIO) CK1xAudioWave(Adapter);
    if (!Miniport)
        return STATUS_INSUFFICIENT_RESOURCES;
    Miniport->AddRef();
    *Unknown = static_cast<PMINIPORTWAVERT>(Miniport);
    return STATUS_SUCCESS;
}
