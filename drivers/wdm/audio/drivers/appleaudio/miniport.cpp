/*
 * PROJECT:         LiberNT Apple Silicon Audio Driver
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         WaveRT and topology miniports for the headphone jack and the speakers
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

static const ULONG AppleAudioTopologyPinCount = 4;

static KSDATARANGE_AUDIO AppleAudioPcmDataRange =
{
    {{
        sizeof(KSDATARANGE_AUDIO),
        0,
        0,
        0,
        {STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO)},
        {STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM)},
        {STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)}
    }},
    APPLE_AUDIO_CHANNELS,
    16,
    32,
    APPLE_AUDIO_SAMPLE_RATE,
    APPLE_AUDIO_SAMPLE_RATE
};

static PKSDATARANGE AppleAudioPcmDataRanges[] =
{
    reinterpret_cast<PKSDATARANGE>(&AppleAudioPcmDataRange)
};

static KSDATARANGE_AUDIO AppleAudioCaptureDataRange =
{
    {{
        sizeof(KSDATARANGE_AUDIO),
        0,
        0,
        0,
        {STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO)},
        {STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM)},
        {STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)}
    }},
    APPLE_AUDIO_CAPTURE_CHANNELS,
    16,
    32,
    APPLE_AUDIO_SAMPLE_RATE,
    APPLE_AUDIO_SAMPLE_RATE
};

static PKSDATARANGE AppleAudioCaptureDataRanges[] =
{
    reinterpret_cast<PKSDATARANGE>(&AppleAudioCaptureDataRange)
};

static KSDATARANGE AppleAudioBridgeDataRange =
{{
    sizeof(KSDATARANGE),
    0,
    0,
    0,
    {STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO)},
    {STATICGUIDOF(KSDATAFORMAT_SUBTYPE_ANALOG)},
    {STATICGUIDOF(KSDATAFORMAT_SPECIFIER_NONE)}
}};

static PKSDATARANGE AppleAudioBridgeDataRanges[] =
{
    &AppleAudioBridgeDataRange
};

static NTSTATUS
AppleAudioBasicSupport(
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

static CAppleAudioAdapter *
AppleAudioTopologyAdapter(PPCPROPERTY_REQUEST PropertyRequest)
{
    return static_cast<CAppleAudioTopology *>(static_cast<PMINIPORTTOPOLOGY>(PropertyRequest->MajorTarget))->GetAdapter();
}

static ULONG
AppleAudioTopologyEndpoint(PPCPROPERTY_REQUEST PropertyRequest)
{
    return static_cast<CAppleAudioTopology *>(static_cast<PMINIPORTTOPOLOGY>(PropertyRequest->MajorTarget))->GetEndpoint();
}

static NTSTATUS NTAPI
AppleAudioVolumePropertyHandler(PPCPROPERTY_REQUEST PropertyRequest)
{
    const ULONG DescriptionSize = sizeof(KSPROPERTY_DESCRIPTION) + sizeof(KSPROPERTY_MEMBERSHEADER) +
                                  APPLE_AUDIO_CHANNELS * sizeof(KSPROPERTY_STEPPING_LONG);
    ULONG Channel;

    if (!PropertyRequest || PropertyRequest->Node != 0 || !PropertyRequest->MajorTarget)
        return STATUS_INVALID_PARAMETER;

    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
    {
        NTSTATUS Status = AppleAudioBasicSupport(PropertyRequest,
                                                 KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET,
                                                 VT_I4, DescriptionSize, 1);
        if (Status != STATUS_MORE_ENTRIES)
            return Status;

        PKSPROPERTY_DESCRIPTION Description = static_cast<PKSPROPERTY_DESCRIPTION>(PropertyRequest->Value);
        PKSPROPERTY_MEMBERSHEADER Members = reinterpret_cast<PKSPROPERTY_MEMBERSHEADER>(Description + 1);
        Members->MembersFlags = KSPROPERTY_MEMBER_STEPPEDRANGES;
        Members->MembersSize = sizeof(KSPROPERTY_STEPPING_LONG);
        Members->MembersCount = APPLE_AUDIO_CHANNELS;
        Members->Flags = KSPROPERTY_MEMBER_FLAG_BASICSUPPORT_MULTICHANNEL;

        PKSPROPERTY_STEPPING_LONG Range = reinterpret_cast<PKSPROPERTY_STEPPING_LONG>(Members + 1);
        for (ULONG Index = 0; Index < APPLE_AUDIO_CHANNELS; ++Index)
        {
            Range[Index].SteppingDelta = APPLE_AUDIO_KS_VOLUME_STEP;
            Range[Index].Bounds.SignedMinimum =
                (AppleAudioTopologyEndpoint(PropertyRequest) == APPLE_AUDIO_ENDPOINT_SPEAKERS) ?
                APPLE_AUDIO_KS_SPEAKER_VOLUME_MINIMUM : APPLE_AUDIO_KS_VOLUME_MINIMUM;
            Range[Index].Bounds.SignedMaximum = APPLE_AUDIO_KS_VOLUME_MAXIMUM;
        }
        PropertyRequest->ValueSize = DescriptionSize;
        return STATUS_SUCCESS;
    }

    if (PropertyRequest->InstanceSize < sizeof(LONG) || PropertyRequest->ValueSize < sizeof(LONG))
    {
        PropertyRequest->ValueSize = sizeof(LONG);
        return STATUS_BUFFER_TOO_SMALL;
    }
    Channel = *static_cast<PULONG>(PropertyRequest->Instance);
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_GET)
    {
        NTSTATUS Status = AppleAudioTopologyAdapter(PropertyRequest)->GetVolume(AppleAudioTopologyEndpoint(PropertyRequest),
                                                                                Channel,
                                                                                static_cast<PLONG>(PropertyRequest->Value));
        if (NT_SUCCESS(Status))
            PropertyRequest->ValueSize = sizeof(LONG);
        return Status;
    }
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_SET)
        return AppleAudioTopologyAdapter(PropertyRequest)->SetVolume(AppleAudioTopologyEndpoint(PropertyRequest), Channel,
                                                                     *static_cast<PLONG>(PropertyRequest->Value));
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS NTAPI
AppleAudioMutePropertyHandler(PPCPROPERTY_REQUEST PropertyRequest)
{
    const ULONG DescriptionSize = sizeof(KSPROPERTY_DESCRIPTION) + sizeof(KSPROPERTY_MEMBERSHEADER) +
                                  APPLE_AUDIO_CHANNELS * sizeof(KSPROPERTY_STEPPING_LONG);

    if (!PropertyRequest || PropertyRequest->Node != 1 || !PropertyRequest->MajorTarget)
        return STATUS_INVALID_PARAMETER;

    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
    {
        NTSTATUS Status = AppleAudioBasicSupport(PropertyRequest,
                                                 KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET,
                                                 VT_BOOL, DescriptionSize, 1);
        if (Status != STATUS_MORE_ENTRIES)
            return Status;

        PKSPROPERTY_DESCRIPTION Description = static_cast<PKSPROPERTY_DESCRIPTION>(PropertyRequest->Value);
        PKSPROPERTY_MEMBERSHEADER Members = reinterpret_cast<PKSPROPERTY_MEMBERSHEADER>(Description + 1);
        Members->MembersFlags = KSPROPERTY_MEMBER_STEPPEDRANGES;
        Members->MembersSize = sizeof(KSPROPERTY_STEPPING_LONG);
        Members->MembersCount = APPLE_AUDIO_CHANNELS;
        Members->Flags = KSPROPERTY_MEMBER_FLAG_BASICSUPPORT_MULTICHANNEL | KSPROPERTY_MEMBER_FLAG_BASICSUPPORT_UNIFORM;

        PKSPROPERTY_STEPPING_LONG Range = reinterpret_cast<PKSPROPERTY_STEPPING_LONG>(Members + 1);
        for (ULONG Index = 0; Index < APPLE_AUDIO_CHANNELS; ++Index)
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
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_GET)
    {
        *static_cast<PBOOL>(PropertyRequest->Value) =
            AppleAudioTopologyAdapter(PropertyRequest)->GetMute(AppleAudioTopologyEndpoint(PropertyRequest));
        PropertyRequest->ValueSize = sizeof(BOOL);
        return STATUS_SUCCESS;
    }
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_SET)
        return AppleAudioTopologyAdapter(PropertyRequest)->SetMute(AppleAudioTopologyEndpoint(PropertyRequest),
                                                                   *static_cast<PBOOL>(PropertyRequest->Value) != FALSE);
    return STATUS_NOT_SUPPORTED;
}

template <typename Description>
static NTSTATUS
AppleAudioPrepareJackDescription(
    PPCPROPERTY_REQUEST PropertyRequest,
    ULONG PinId,
    Description **JackDescription)
{
    PKSMULTIPLE_ITEM MultipleItem;
    ULONG DescriptionCount =
        (PinId == APPLE_AUDIO_TOPOLOGY_HEADPHONE_PIN || PinId == APPLE_AUDIO_TOPOLOGY_MICROPHONE_PIN) ? 1 : 0;
    ULONG RequiredSize = sizeof(KSMULTIPLE_ITEM) + DescriptionCount * sizeof(Description);

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
    MultipleItem = static_cast<PKSMULTIPLE_ITEM>(PropertyRequest->Value);
    MultipleItem->Size = RequiredSize;
    MultipleItem->Count = DescriptionCount;
    *JackDescription = DescriptionCount ? reinterpret_cast<Description *>(MultipleItem + 1) : NULL;
    PropertyRequest->ValueSize = RequiredSize;
    return STATUS_SUCCESS;
}

static NTSTATUS
AppleAudioJackRequestPin(PPCPROPERTY_REQUEST PropertyRequest, PULONG PinId)
{
    if (!PropertyRequest || !PropertyRequest->MajorTarget || !PropertyRequest->Instance ||
        PropertyRequest->InstanceSize < sizeof(ULONG))
        return STATUS_INVALID_PARAMETER;
    *PinId = *static_cast<PULONG>(PropertyRequest->Instance);
    return *PinId < AppleAudioTopologyPinCount ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

static NTSTATUS NTAPI
AppleAudioJackDescriptionPropertyHandler(PPCPROPERTY_REQUEST PropertyRequest)
{
    PKSJACK_DESCRIPTION JackDescription;
    ULONG PinId;
    NTSTATUS Status;

    Status = AppleAudioJackRequestPin(PropertyRequest, &PinId);
    if (!NT_SUCCESS(Status))
        return Status;
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
        return AppleAudioBasicSupport(PropertyRequest, KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET, VT_ILLEGAL,
                                      sizeof(KSPROPERTY_DESCRIPTION), 0);
    if (!(PropertyRequest->Verb & KSPROPERTY_TYPE_GET))
        return STATUS_NOT_SUPPORTED;
    Status = AppleAudioPrepareJackDescription(PropertyRequest, PinId, &JackDescription);
    if (!NT_SUCCESS(Status) || !JackDescription)
        return Status;

    RtlZeroMemory(JackDescription, sizeof(*JackDescription));
    JackDescription->ChannelMapping =
        (PinId == APPLE_AUDIO_TOPOLOGY_MICROPHONE_PIN) ? KSAUDIO_SPEAKER_MONO : KSAUDIO_SPEAKER_STEREO;
    JackDescription->ConnectionType = eConnType3Point5mm;
    JackDescription->GeoLocation = eGeoLocLeft;
    JackDescription->GenLocation = eGenLocPrimaryBox;
    JackDescription->PortConnection = ePortConnJack;
    JackDescription->IsConnected = (PinId == APPLE_AUDIO_TOPOLOGY_MICROPHONE_PIN) ?
                                   AppleAudioTopologyAdapter(PropertyRequest)->IsMicrophoneConnected() :
                                   AppleAudioTopologyAdapter(PropertyRequest)->IsSinkConnected();
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI
AppleAudioJackDescription2PropertyHandler(PPCPROPERTY_REQUEST PropertyRequest)
{
    PKSJACK_DESCRIPTION2 JackDescription;
    ULONG PinId;
    NTSTATUS Status;

    Status = AppleAudioJackRequestPin(PropertyRequest, &PinId);
    if (!NT_SUCCESS(Status))
        return Status;
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
        return AppleAudioBasicSupport(PropertyRequest, KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET, VT_ILLEGAL,
                                      sizeof(KSPROPERTY_DESCRIPTION), 0);
    if (!(PropertyRequest->Verb & KSPROPERTY_TYPE_GET))
        return STATUS_NOT_SUPPORTED;
    Status = AppleAudioPrepareJackDescription(PropertyRequest, PinId, &JackDescription);
    if (!NT_SUCCESS(Status) || !JackDescription)
        return Status;

    RtlZeroMemory(JackDescription, sizeof(*JackDescription));
    JackDescription->JackCapabilities = JACKDESC2_PRESENCE_DETECT_CAPABILITY;
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI
AppleAudioSpeakerJackDescriptionPropertyHandler(PPCPROPERTY_REQUEST PropertyRequest)
{
    PKSMULTIPLE_ITEM MultipleItem;
    PKSJACK_DESCRIPTION JackDescription;
    ULONG PinId, Count, RequiredSize;

    if (!PropertyRequest || !PropertyRequest->MajorTarget || !PropertyRequest->Instance ||
        PropertyRequest->InstanceSize < sizeof(ULONG))
        return STATUS_INVALID_PARAMETER;
    PinId = *static_cast<PULONG>(PropertyRequest->Instance);
    if (PinId > APPLE_AUDIO_SPEAKER_TOPOLOGY_SPEAKER_PIN)
        return STATUS_INVALID_PARAMETER;
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
        return AppleAudioBasicSupport(PropertyRequest, KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET, VT_ILLEGAL,
                                      sizeof(KSPROPERTY_DESCRIPTION), 0);
    if (!(PropertyRequest->Verb & KSPROPERTY_TYPE_GET))
        return STATUS_NOT_SUPPORTED;
    Count = (PinId == APPLE_AUDIO_SPEAKER_TOPOLOGY_SPEAKER_PIN) ? 1 : 0;
    RequiredSize = sizeof(KSMULTIPLE_ITEM) + Count * sizeof(KSJACK_DESCRIPTION);
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
    MultipleItem = static_cast<PKSMULTIPLE_ITEM>(PropertyRequest->Value);
    MultipleItem->Size = RequiredSize;
    MultipleItem->Count = Count;
    PropertyRequest->ValueSize = RequiredSize;
    if (!Count)
        return STATUS_SUCCESS;
    JackDescription = reinterpret_cast<PKSJACK_DESCRIPTION>(MultipleItem + 1);
    RtlZeroMemory(JackDescription, sizeof(*JackDescription));
    JackDescription->ChannelMapping = KSAUDIO_SPEAKER_STEREO;
    JackDescription->ConnectionType = eConnTypeUnknown;
    JackDescription->GeoLocation = eGeoLocFront;
    JackDescription->GenLocation = eGenLocPrimaryBox;
    JackDescription->PortConnection = ePortConnIntegratedDevice;
    JackDescription->IsConnected = TRUE;
    return STATUS_SUCCESS;
}

static PCPROPERTY_ITEM AppleAudioVolumeProperties[] =
{
    {&KSPROPSETID_Audio, KSPROPERTY_AUDIO_VOLUMELEVEL,
     KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT, AppleAudioVolumePropertyHandler}
};

static PCPROPERTY_ITEM AppleAudioMuteProperties[] =
{
    {&KSPROPSETID_Audio, KSPROPERTY_AUDIO_MUTE,
     KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT, AppleAudioMutePropertyHandler}
};

static PCPROPERTY_ITEM AppleAudioJackProperties[] =
{
    {&KSPROPSETID_Jack, KSPROPERTY_JACK_DESCRIPTION, KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT,
     AppleAudioJackDescriptionPropertyHandler},
    {&KSPROPSETID_Jack, KSPROPERTY_JACK_DESCRIPTION2, KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT,
     AppleAudioJackDescription2PropertyHandler}
};

static NTSTATUS NTAPI
AppleAudioJackEventHandler(PPCEVENT_REQUEST EventRequest)
{
    if (!EventRequest || !EventRequest->MajorTarget)
        return STATUS_INVALID_PARAMETER;
    switch (EventRequest->Verb)
    {
        case PCEVENT_VERB_ADD:
            if (!EventRequest->EventEntry)
                return STATUS_INVALID_PARAMETER;
            static_cast<CAppleAudioTopology *>(static_cast<PMINIPORTTOPOLOGY>(EventRequest->MajorTarget))
                ->AddEventToEventList(EventRequest->EventEntry);
            return STATUS_SUCCESS;
        case PCEVENT_VERB_REMOVE:
        case PCEVENT_VERB_SUPPORT:
            return STATUS_SUCCESS;
        default:
            return STATUS_INVALID_PARAMETER;
    }
}

static PCEVENT_ITEM AppleAudioJackEvents[] =
{
    {&KSEVENTSETID_PinCapsChange, KSEVENT_PINCAPS_JACKINFOCHANGE,
     PCEVENT_ITEM_FLAG_ENABLE | PCEVENT_ITEM_FLAG_BASICSUPPORT, AppleAudioJackEventHandler}
};

DEFINE_PCAUTOMATION_TABLE_PROP(AppleAudioVolumeAutomation, AppleAudioVolumeProperties);
DEFINE_PCAUTOMATION_TABLE_PROP(AppleAudioMuteAutomation, AppleAudioMuteProperties);
DEFINE_PCAUTOMATION_TABLE_PROP_EVENT(AppleAudioJackAutomation, AppleAudioJackProperties, AppleAudioJackEvents);

static PCPROPERTY_ITEM AppleAudioSpeakerJackProperties[] =
{
    {&KSPROPSETID_Jack, KSPROPERTY_JACK_DESCRIPTION, KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT,
     AppleAudioSpeakerJackDescriptionPropertyHandler}
};

DEFINE_PCAUTOMATION_TABLE_PROP(AppleAudioSpeakerJackAutomation, AppleAudioSpeakerJackProperties);

static PCPIN_DESCRIPTOR AppleAudioWavePins[] =
{
    {
        1, 1, 0, NULL,
        {
            0, NULL, 0, NULL,
            RTL_NUMBER_OF(AppleAudioPcmDataRanges), AppleAudioPcmDataRanges,
            KSPIN_DATAFLOW_IN, KSPIN_COMMUNICATION_SINK, &KSCATEGORY_AUDIO, NULL, {0}
        }
    },
    {
        0, 0, 0, NULL,
        {
            0, NULL, 0, NULL,
            RTL_NUMBER_OF(AppleAudioBridgeDataRanges), AppleAudioBridgeDataRanges,
            KSPIN_DATAFLOW_OUT, KSPIN_COMMUNICATION_NONE, &KSCATEGORY_AUDIO, NULL, {0}
        }
    },
    {
        1, 1, 0, NULL,
        {
            0, NULL, 0, NULL,
            RTL_NUMBER_OF(AppleAudioCaptureDataRanges), AppleAudioCaptureDataRanges,
            KSPIN_DATAFLOW_OUT, KSPIN_COMMUNICATION_SINK, &KSCATEGORY_AUDIO, NULL, {0}
        }
    },
    {
        0, 0, 0, NULL,
        {
            0, NULL, 0, NULL,
            RTL_NUMBER_OF(AppleAudioBridgeDataRanges), AppleAudioBridgeDataRanges,
            KSPIN_DATAFLOW_IN, KSPIN_COMMUNICATION_NONE, &KSCATEGORY_AUDIO, NULL, {0}
        }
    }
};

static PCNODE_DESCRIPTOR AppleAudioWaveNodes[] =
{
    {0, NULL, &KSNODETYPE_DAC, NULL},
    {0, NULL, &KSNODETYPE_ADC, NULL}
};

static PCCONNECTION_DESCRIPTOR AppleAudioWaveConnections[] =
{
    {PCFILTER_NODE, APPLE_AUDIO_WAVE_RENDER_PIN, 0, 1},
    {0, 0, PCFILTER_NODE, 1},
    {PCFILTER_NODE, APPLE_AUDIO_WAVE_CAPTURE_BRIDGE_PIN, 1, 1},
    {1, 0, PCFILTER_NODE, APPLE_AUDIO_WAVE_CAPTURE_PIN}
};

static PCFILTER_DESCRIPTOR AppleAudioWaveFilterDescriptor =
{
    0, NULL,
    sizeof(PCPIN_DESCRIPTOR), RTL_NUMBER_OF(AppleAudioWavePins), AppleAudioWavePins,
    sizeof(PCNODE_DESCRIPTOR), RTL_NUMBER_OF(AppleAudioWaveNodes), AppleAudioWaveNodes,
    RTL_NUMBER_OF(AppleAudioWaveConnections), AppleAudioWaveConnections,
    0, NULL
};

static PCPIN_DESCRIPTOR AppleAudioTopologyPins[] =
{
    {
        0, 0, 0, NULL,
        {
            0, NULL, 0, NULL,
            RTL_NUMBER_OF(AppleAudioBridgeDataRanges), AppleAudioBridgeDataRanges,
            KSPIN_DATAFLOW_IN, KSPIN_COMMUNICATION_NONE, &KSCATEGORY_AUDIO, NULL, {0}
        }
    },
    {
        0, 0, 0, NULL,
        {
            0, NULL, 0, NULL,
            RTL_NUMBER_OF(AppleAudioBridgeDataRanges), AppleAudioBridgeDataRanges,
            KSPIN_DATAFLOW_OUT, KSPIN_COMMUNICATION_NONE, &KSNODETYPE_HEADPHONES, NULL, {0}
        }
    },
    {
        0, 0, 0, NULL,
        {
            0, NULL, 0, NULL,
            RTL_NUMBER_OF(AppleAudioBridgeDataRanges), AppleAudioBridgeDataRanges,
            KSPIN_DATAFLOW_IN, KSPIN_COMMUNICATION_NONE, &KSNODETYPE_HEADSET_MICROPHONE, NULL, {0}
        }
    },
    {
        0, 0, 0, NULL,
        {
            0, NULL, 0, NULL,
            RTL_NUMBER_OF(AppleAudioBridgeDataRanges), AppleAudioBridgeDataRanges,
            KSPIN_DATAFLOW_OUT, KSPIN_COMMUNICATION_NONE, &KSCATEGORY_AUDIO, NULL, {0}
        }
    }
};

static PCNODE_DESCRIPTOR AppleAudioTopologyNodes[] =
{
    {0, &AppleAudioVolumeAutomation, &KSNODETYPE_VOLUME, &KSAUDFNAME_MASTER_VOLUME},
    {0, &AppleAudioMuteAutomation, &KSNODETYPE_MUTE, &KSAUDFNAME_MASTER_MUTE}
};

static PCCONNECTION_DESCRIPTOR AppleAudioTopologyConnections[] =
{
    {PCFILTER_NODE, 0, 0, 1},
    {0, 0, 1, 1},
    {1, 0, PCFILTER_NODE, APPLE_AUDIO_TOPOLOGY_HEADPHONE_PIN},
    {PCFILTER_NODE, APPLE_AUDIO_TOPOLOGY_MICROPHONE_PIN, PCFILTER_NODE, APPLE_AUDIO_TOPOLOGY_CAPTURE_PIN}
};

static PCFILTER_DESCRIPTOR AppleAudioTopologyFilterDescriptor =
{
    0, &AppleAudioJackAutomation,
    sizeof(PCPIN_DESCRIPTOR), RTL_NUMBER_OF(AppleAudioTopologyPins), AppleAudioTopologyPins,
    sizeof(PCNODE_DESCRIPTOR), RTL_NUMBER_OF(AppleAudioTopologyNodes), AppleAudioTopologyNodes,
    RTL_NUMBER_OF(AppleAudioTopologyConnections), AppleAudioTopologyConnections,
    0, NULL
};

static PCPIN_DESCRIPTOR AppleAudioSpeakerWavePins[] =
{
    {
        1, 1, 0, NULL,
        {
            0, NULL, 0, NULL,
            RTL_NUMBER_OF(AppleAudioPcmDataRanges), AppleAudioPcmDataRanges,
            KSPIN_DATAFLOW_IN, KSPIN_COMMUNICATION_SINK, &KSCATEGORY_AUDIO, NULL, {0}
        }
    },
    {
        0, 0, 0, NULL,
        {
            0, NULL, 0, NULL,
            RTL_NUMBER_OF(AppleAudioBridgeDataRanges), AppleAudioBridgeDataRanges,
            KSPIN_DATAFLOW_OUT, KSPIN_COMMUNICATION_NONE, &KSCATEGORY_AUDIO, NULL, {0}
        }
    }
};

static PCNODE_DESCRIPTOR AppleAudioSpeakerWaveNodes[] =
{
    {0, NULL, &KSNODETYPE_DAC, NULL}
};

static PCCONNECTION_DESCRIPTOR AppleAudioSpeakerWaveConnections[] =
{
    {PCFILTER_NODE, APPLE_AUDIO_WAVE_RENDER_PIN, 0, 1},
    {0, 0, PCFILTER_NODE, 1}
};

static PCFILTER_DESCRIPTOR AppleAudioSpeakerWaveFilterDescriptor =
{
    0, NULL,
    sizeof(PCPIN_DESCRIPTOR), RTL_NUMBER_OF(AppleAudioSpeakerWavePins), AppleAudioSpeakerWavePins,
    sizeof(PCNODE_DESCRIPTOR), RTL_NUMBER_OF(AppleAudioSpeakerWaveNodes), AppleAudioSpeakerWaveNodes,
    RTL_NUMBER_OF(AppleAudioSpeakerWaveConnections), AppleAudioSpeakerWaveConnections,
    0, NULL
};

static PCPIN_DESCRIPTOR AppleAudioSpeakerTopologyPins[] =
{
    {
        0, 0, 0, NULL,
        {
            0, NULL, 0, NULL,
            RTL_NUMBER_OF(AppleAudioBridgeDataRanges), AppleAudioBridgeDataRanges,
            KSPIN_DATAFLOW_IN, KSPIN_COMMUNICATION_NONE, &KSCATEGORY_AUDIO, NULL, {0}
        }
    },
    {
        0, 0, 0, NULL,
        {
            0, NULL, 0, NULL,
            RTL_NUMBER_OF(AppleAudioBridgeDataRanges), AppleAudioBridgeDataRanges,
            KSPIN_DATAFLOW_OUT, KSPIN_COMMUNICATION_NONE, &KSNODETYPE_SPEAKER, NULL, {0}
        }
    }
};

static PCCONNECTION_DESCRIPTOR AppleAudioSpeakerTopologyConnections[] =
{
    {PCFILTER_NODE, 0, 0, 1},
    {0, 0, 1, 1},
    {1, 0, PCFILTER_NODE, APPLE_AUDIO_SPEAKER_TOPOLOGY_SPEAKER_PIN}
};

static PCFILTER_DESCRIPTOR AppleAudioSpeakerTopologyFilterDescriptor =
{
    0, &AppleAudioSpeakerJackAutomation,
    sizeof(PCPIN_DESCRIPTOR), RTL_NUMBER_OF(AppleAudioSpeakerTopologyPins), AppleAudioSpeakerTopologyPins,
    sizeof(PCNODE_DESCRIPTOR), RTL_NUMBER_OF(AppleAudioTopologyNodes), AppleAudioTopologyNodes,
    RTL_NUMBER_OF(AppleAudioSpeakerTopologyConnections), AppleAudioSpeakerTopologyConnections,
    0, NULL
};

ULONG
AppleAudioFormatSampleBits(PKSDATAFORMAT DataFormat, ULONG Channels)
{
    PKSDATAFORMAT_WAVEFORMATEX WaveFormat;
    PWAVEFORMATEXTENSIBLE Extensible;
    ULONG Bits, ValidBits;

    if (!DataFormat || DataFormat->FormatSize < sizeof(KSDATAFORMAT_WAVEFORMATEX) ||
        !IsEqualGUIDAligned(DataFormat->MajorFormat, KSDATAFORMAT_TYPE_AUDIO) ||
        !IsEqualGUIDAligned(DataFormat->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) ||
        !IsEqualGUIDAligned(DataFormat->Specifier, KSDATAFORMAT_SPECIFIER_WAVEFORMATEX))
        return 0;

    WaveFormat = reinterpret_cast<PKSDATAFORMAT_WAVEFORMATEX>(DataFormat);
    Bits = WaveFormat->WaveFormatEx.wBitsPerSample;
    ValidBits = Bits;
    if (WaveFormat->WaveFormatEx.wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        if (DataFormat->FormatSize < sizeof(KSDATAFORMAT) + sizeof(WAVEFORMATEXTENSIBLE) ||
            WaveFormat->WaveFormatEx.cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
            return 0;
        Extensible = reinterpret_cast<PWAVEFORMATEXTENSIBLE>(&WaveFormat->WaveFormatEx);
        if (!IsEqualGUIDAligned(Extensible->SubFormat, KSDATAFORMAT_SUBTYPE_PCM))
            return 0;
        ValidBits = Extensible->Samples.wValidBitsPerSample;
    }
    else if (WaveFormat->WaveFormatEx.wFormatTag != WAVE_FORMAT_PCM)
    {
        return 0;
    }

    if (!((Bits == 16 && ValidBits == 16) || (Bits == 32 && (ValidBits == 24 || ValidBits == 32))))
        return 0;
    if (WaveFormat->WaveFormatEx.nChannels != Channels ||
        WaveFormat->WaveFormatEx.nSamplesPerSec != APPLE_AUDIO_SAMPLE_RATE ||
        WaveFormat->WaveFormatEx.nBlockAlign != Channels * Bits / 8 ||
        WaveFormat->WaveFormatEx.nAvgBytesPerSec != APPLE_AUDIO_SAMPLE_RATE * Channels * Bits / 8)
        return 0;
    return Bits;
}

CAppleAudioTopology::CAppleAudioTopology(CAppleAudioAdapter *Adapter, ULONG Endpoint)
    : m_Adapter(Adapter), m_PortEvents(NULL), m_Endpoint(Endpoint)
{
    m_Adapter->AddRef();
}

CAppleAudioTopology::~CAppleAudioTopology()
{
    if (m_PortEvents)
        m_PortEvents->Release();
    m_Adapter->Release();
}

NTSTATUS
NTAPI
CAppleAudioTopology::QueryInterface(REFIID InterfaceId, PVOID *Interface)
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
CAppleAudioTopology::GetDescription(PPCFILTER_DESCRIPTOR *Description)
{
    if (!Description)
        return STATUS_INVALID_PARAMETER;
    *Description = (m_Endpoint == APPLE_AUDIO_ENDPOINT_SPEAKERS) ? &AppleAudioSpeakerTopologyFilterDescriptor :
                   &AppleAudioTopologyFilterDescriptor;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CAppleAudioTopology::DataRangeIntersection(
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
CAppleAudioTopology::Init(PUNKNOWN UnknownAdapter, PRESOURCELIST ResourceList, PPORTTOPOLOGY Port)
{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(UnknownAdapter);
    UNREFERENCED_PARAMETER(ResourceList);
    if (!Port)
        return STATUS_INVALID_PARAMETER;
    if (m_Endpoint == APPLE_AUDIO_ENDPOINT_SPEAKERS)
        return STATUS_SUCCESS;
    Status = Port->QueryInterface(IID_IPortEvents, reinterpret_cast<PVOID *>(&m_PortEvents));
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
CAppleAudioTopology::AddEventToEventList(PKSEVENT_ENTRY EventEntry)
{
    if (m_PortEvents && EventEntry)
        m_PortEvents->AddEventToEventList(EventEntry);
}

CAppleAudioWave::CAppleAudioWave(CAppleAudioAdapter *Adapter, ULONG Endpoint) : m_Adapter(Adapter), m_Endpoint(Endpoint)
{
    m_Adapter->AddRef();
}

CAppleAudioWave::~CAppleAudioWave()
{
    m_Adapter->Release();
}

NTSTATUS
NTAPI
CAppleAudioWave::QueryInterface(REFIID InterfaceId, PVOID *Interface)
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
CAppleAudioWave::GetDescription(PPCFILTER_DESCRIPTOR *Description)
{
    if (!Description)
        return STATUS_INVALID_PARAMETER;
    *Description = (m_Endpoint == APPLE_AUDIO_ENDPOINT_SPEAKERS) ? &AppleAudioSpeakerWaveFilterDescriptor :
                   &AppleAudioWaveFilterDescriptor;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CAppleAudioWave::DataRangeIntersection(
    ULONG PinId,
    PKSDATARANGE DataRange,
    PKSDATARANGE MatchingDataRange,
    ULONG OutputBufferLength,
    PVOID ResultantFormat,
    PULONG ResultantFormatLength)
{
    PKSDATARANGE_AUDIO AudioRange;
    PKSDATAFORMAT_WAVEFORMATEX Format;
    ULONG Channels;

    if (!ResultantFormatLength || !DataRange || !MatchingDataRange ||
        (PinId != APPLE_AUDIO_WAVE_RENDER_PIN && PinId != APPLE_AUDIO_WAVE_CAPTURE_PIN) ||
        (m_Endpoint == APPLE_AUDIO_ENDPOINT_SPEAKERS && PinId != APPLE_AUDIO_WAVE_RENDER_PIN))
        return STATUS_INVALID_PARAMETER;
    Channels = (PinId == APPLE_AUDIO_WAVE_RENDER_PIN) ? APPLE_AUDIO_CHANNELS : APPLE_AUDIO_CAPTURE_CHANNELS;
    if (!IsEqualGUIDAligned(DataRange->MajorFormat, KSDATAFORMAT_TYPE_AUDIO) ||
        !IsEqualGUIDAligned(DataRange->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) ||
        !IsEqualGUIDAligned(DataRange->Specifier, KSDATAFORMAT_SPECIFIER_WAVEFORMATEX))
        return STATUS_NO_MATCH;

    if (AppleAudioFormatSampleBits(reinterpret_cast<PKSDATAFORMAT>(DataRange), Channels))
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
    if (AudioRange->MaximumChannels < Channels || AudioRange->MinimumBitsPerSample > 16 ||
        AudioRange->MaximumBitsPerSample < 16 || AudioRange->MinimumSampleFrequency > APPLE_AUDIO_SAMPLE_RATE ||
        AudioRange->MaximumSampleFrequency < APPLE_AUDIO_SAMPLE_RATE)
        return STATUS_NO_MATCH;

    *ResultantFormatLength = sizeof(KSDATAFORMAT_WAVEFORMATEX);
    if (!OutputBufferLength || !ResultantFormat)
        return STATUS_BUFFER_OVERFLOW;
    if (OutputBufferLength < sizeof(KSDATAFORMAT_WAVEFORMATEX))
        return STATUS_BUFFER_TOO_SMALL;
    Format = static_cast<PKSDATAFORMAT_WAVEFORMATEX>(ResultantFormat);
    RtlZeroMemory(Format, sizeof(*Format));
    Format->DataFormat.FormatSize = sizeof(KSDATAFORMAT_WAVEFORMATEX);
    Format->DataFormat.SampleSize = Channels * 2;
    Format->DataFormat.MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
    Format->DataFormat.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    Format->DataFormat.Specifier = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;
    Format->WaveFormatEx.wFormatTag = WAVE_FORMAT_PCM;
    Format->WaveFormatEx.nChannels = (WORD)Channels;
    Format->WaveFormatEx.nSamplesPerSec = APPLE_AUDIO_SAMPLE_RATE;
    Format->WaveFormatEx.wBitsPerSample = 16;
    Format->WaveFormatEx.nBlockAlign = (WORD)(Channels * 2);
    Format->WaveFormatEx.nAvgBytesPerSec = APPLE_AUDIO_SAMPLE_RATE * Channels * 2;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CAppleAudioWave::Init(PUNKNOWN UnknownAdapter, PRESOURCELIST ResourceList, PPORTWAVERT Port)
{
    UNREFERENCED_PARAMETER(UnknownAdapter);
    UNREFERENCED_PARAMETER(ResourceList);
    UNREFERENCED_PARAMETER(Port);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CAppleAudioWave::NewStream(
    PMINIPORTWAVERTSTREAM *Stream,
    PPORTWAVERTSTREAM PortStream,
    ULONG Pin,
    BOOLEAN Capture,
    PKSDATAFORMAT DataFormat)
{
    CAppleAudioStream *NewStream;
    ULONG Direction, SampleBits;

    if (!Stream || !PortStream)
        return STATUS_INVALID_PARAMETER;
    if (m_Endpoint == APPLE_AUDIO_ENDPOINT_SPEAKERS && Pin == APPLE_AUDIO_WAVE_RENDER_PIN && !Capture &&
        m_Adapter->HasSpeakers())
        Direction = APPLE_AUDIO_SPEAKERS;
    else if (m_Endpoint == APPLE_AUDIO_ENDPOINT_SPEAKERS)
        return STATUS_NOT_SUPPORTED;
    else if (Pin == APPLE_AUDIO_WAVE_RENDER_PIN && !Capture)
        Direction = APPLE_AUDIO_RENDER;
    else if (Pin == APPLE_AUDIO_WAVE_CAPTURE_PIN && Capture && m_Adapter->HasCapture())
        Direction = APPLE_AUDIO_CAPTURE;
    else
        return STATUS_NOT_SUPPORTED;
    SampleBits = AppleAudioFormatSampleBits(DataFormat, APPLE_AUDIO_STREAM_CHANNELS(Direction));
    if (!SampleBits)
        return STATUS_NOT_SUPPORTED;
    if (!m_Adapter->ClaimStream(Direction))
        return STATUS_DEVICE_BUSY;
    NewStream = new (NonPagedPool, TAG_APPLE_AUDIO) CAppleAudioStream(m_Adapter, PortStream, Direction, SampleBits);
    if (!NewStream)
    {
        m_Adapter->ReleaseStream(Direction);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    NewStream->AddRef();
    *Stream = static_cast<PMINIPORTWAVERTSTREAM>(NewStream);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CAppleAudioWave::GetDeviceDescription(PDEVICE_DESCRIPTION DeviceDescription)
{
    if (!DeviceDescription)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(DeviceDescription, sizeof(*DeviceDescription));
    DeviceDescription->Version = DEVICE_DESCRIPTION_VERSION1;
    DeviceDescription->Master = TRUE;
    DeviceDescription->ScatterGather = FALSE;
    DeviceDescription->Dma64BitAddresses = TRUE;
    DeviceDescription->InterfaceType = Internal;
    DeviceDescription->MaximumLength = APPLE_AUDIO_MAX_BUFFER_SIZE;
    return STATUS_SUCCESS;
}

NTSTATUS
AppleAudioCreateTopology(PUNKNOWN *Unknown, CAppleAudioAdapter *Adapter, ULONG Endpoint)
{
    CAppleAudioTopology *Miniport;

    if (!Unknown || !Adapter || Endpoint >= APPLE_AUDIO_ENDPOINTS)
        return STATUS_INVALID_PARAMETER;
    Miniport = new (NonPagedPool, TAG_APPLE_AUDIO) CAppleAudioTopology(Adapter, Endpoint);
    if (!Miniport)
        return STATUS_INSUFFICIENT_RESOURCES;
    Miniport->AddRef();
    *Unknown = static_cast<PMINIPORTTOPOLOGY>(Miniport);
    return STATUS_SUCCESS;
}

NTSTATUS
AppleAudioCreateWave(PUNKNOWN *Unknown, CAppleAudioAdapter *Adapter, ULONG Endpoint)
{
    CAppleAudioWave *Miniport;

    if (!Unknown || !Adapter || Endpoint >= APPLE_AUDIO_ENDPOINTS)
        return STATUS_INVALID_PARAMETER;
    Miniport = new (NonPagedPool, TAG_APPLE_AUDIO) CAppleAudioWave(Adapter, Endpoint);
    if (!Miniport)
        return STATUS_INSUFFICIENT_RESOURCES;
    Miniport->AddRef();
    *Unknown = static_cast<PMINIPORTWAVERT>(Miniport);
    return STATUS_SUCCESS;
}
