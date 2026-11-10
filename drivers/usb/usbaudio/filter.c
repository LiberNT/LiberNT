/*
* PROJECT:     ReactOS Universal Audio Class Driver
* LICENSE:     GPL - See COPYING in the top level directory
* FILE:        drivers/usb/usbaudio/filter.c
* PURPOSE:     USB Audio device driver.
* PROGRAMMERS:
*              Johannes Anderwald (johannes.anderwald@reactos.org)
*/

#include "usbaudio.h"

GUID NodeTypeMicrophone = { STATIC_KSNODETYPE_MICROPHONE };
GUID NodeTypeDesktopMicrophone = { STATIC_KSNODETYPE_DESKTOP_MICROPHONE };
GUID NodeTypePersonalMicrophone = { STATIC_KSNODETYPE_PERSONAL_MICROPHONE };
GUID NodeTypeOmmniMicrophone = { STATIC_KSNODETYPE_OMNI_DIRECTIONAL_MICROPHONE };
GUID NodeTypeArrayMicrophone = { STATIC_KSNODETYPE_MICROPHONE_ARRAY };
GUID NodeTypeProcessingArrayMicrophone = { STATIC_KSNODETYPE_PROCESSING_MICROPHONE_ARRAY };
GUID NodeTypeSpeaker = { STATIC_KSNODETYPE_SPEAKER };
GUID NodeTypeHeadphonesSpeaker = { STATIC_KSNODETYPE_HEADPHONES };
GUID NodeTypeHMDA = { STATIC_KSNODETYPE_HEAD_MOUNTED_DISPLAY_AUDIO };
GUID NodeTypeDesktopSpeaker = { STATIC_KSNODETYPE_DESKTOP_SPEAKER };
GUID NodeTypeRoomSpeaker = { STATIC_KSNODETYPE_ROOM_SPEAKER };
GUID NodeTypeCommunicationSpeaker = { STATIC_KSNODETYPE_COMMUNICATION_SPEAKER };
GUID NodeTypeSubwoofer = { STATIC_KSNODETYPE_LOW_FREQUENCY_EFFECTS_SPEAKER };
GUID NodeTypeBidirectionalUndefined = { STATIC_KSNODETYPE_BIDIRECTIONAL_UNDEFINED };
GUID NodeTypeHandset = { STATIC_KSNODETYPE_HANDSET };
GUID NodeTypeHeadset = { STATIC_KSNODETYPE_HEADSET };
GUID NodeTypeSpeakerphone = { STATIC_KSNODETYPE_SPEAKERPHONE_NO_ECHO_REDUCTION };
GUID NodeTypeEchoSuppressingSpeakerphone = { STATIC_KSNODETYPE_ECHO_SUPPRESSING_SPEAKERPHONE };
GUID NodeTypeEchoCancelingSpeakerphone = { STATIC_KSNODETYPE_ECHO_CANCELING_SPEAKERPHONE };
GUID NodeTypePhoneLine = { STATIC_KSNODETYPE_PHONE_LINE };
GUID NodeTypeLineConnector = { STATIC_KSNODETYPE_LINE_CONNECTOR };
GUID NodeTypeAnalogConnector = { STATIC_KSNODETYPE_ANALOG_CONNECTOR };
GUID NodeTypeCapture = { STATIC_PINNAME_CAPTURE };
GUID NodeTypePlayback = { STATIC_KSCATEGORY_AUDIO };
GUID GUID_KSCATEGORY_AUDIO = { STATIC_KSCATEGORY_AUDIO };

static GUID UsbAudioFilterCategories[] =
{
    { STATIC_KSCATEGORY_AUDIO },
    { STATIC_KSCATEGORY_RENDER },
    { STATIC_KSCATEGORY_CAPTURE },
    { STATIC_KSCATEGORY_AUDIO_DEVICE },
    { STATIC_KSCATEGORY_TOPOLOGY }
};

KSPIN_INTERFACE StandardPinInterface =
{
     {STATIC_KSINTERFACESETID_Standard},
     KSINTERFACE_STANDARD_STREAMING,
     0
};

KSPIN_MEDIUM StandardPinMedium =
{
     {STATIC_KSMEDIUMSETID_Standard},
     KSMEDIUM_TYPE_ANYINSTANCE,
     0
};

LPGUID
UsbAudioGetPinCategoryFromTerminalDescriptor(
    IN PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR TerminalDescriptor);

static
const GUID *
UsbAudioGetTerminalNodeType(
    IN PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR TerminalDescriptor)
{
    USHORT TerminalType = TerminalDescriptor->wTerminalType;

    /*
     * USB streaming terminals are the digital boundary between the host
     * wave pin and the device topology. Model that boundary as the ADC/DAC
     * converter so sysaudio/mmixer can walk from the converter to both the
     * wave pin and the physical bridge pin. Non-streaming terminals keep
     * their physical endpoint role.
     */
    if (TerminalType == USB_AUDIO_STREAMING_TERMINAL_TYPE)
    {
        if (TerminalDescriptor->bDescriptorSubtype == USB_AUDIO_INPUT_TERMINAL)
            return &KSNODETYPE_DAC;

        if (TerminalDescriptor->bDescriptorSubtype == USB_AUDIO_OUTPUT_TERMINAL)
            return &KSNODETYPE_ADC;

        return &KSNODETYPE_SRC;
    }

    return UsbAudioGetPinCategoryFromTerminalDescriptor(TerminalDescriptor);
}

static
VOID
UsbAudioAddTerminalNode(
    IN PKSFILTER_DESCRIPTOR FilterDescriptor,
    IN PKSNODE_DESCRIPTOR NodeDescriptors,
    IN PNODE_CONTEXT NodeContext,
    IN OUT PULONG DescriptorCount,
    IN PUSB_COMMON_DESCRIPTOR CommonDescriptor)
{
    const GUID *NodeType;

    NodeType = UsbAudioGetTerminalNodeType((PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR)CommonDescriptor);

    NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].Type = NodeType;
    NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].Name = NodeType;
    NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].AutomationTable = AllocFunction(sizeof(KSAUTOMATION_TABLE));

    NodeContext[*DescriptorCount].Descriptor = CommonDescriptor;
    NodeContext[*DescriptorCount].NodeCount = 1;
    NodeContext[*DescriptorCount].Nodes[0] = FilterDescriptor->NodeDescriptorsCount;
    (*DescriptorCount)++;

    FilterDescriptor->NodeDescriptorsCount++;
}

KSDATARANGE BridgePinAudioFormat[] =
{
    {
        {
            sizeof(KSDATAFORMAT),
            0,
            0,
            0,
            {STATIC_KSDATAFORMAT_TYPE_AUDIO},
            {STATIC_KSDATAFORMAT_SUBTYPE_ANALOG},
            {STATIC_KSDATAFORMAT_SPECIFIER_NONE}
        }
    }
};

static PKSDATARANGE BridgePinAudioFormats[] =
{
    &BridgePinAudioFormat[0]
};

static LPWSTR ReferenceString = L"global";

NTSTATUS
NTAPI
USBAudioFilterCreate(
    PKSFILTER Filter,
    PIRP Irp);

static KSFILTER_DISPATCH USBAudioFilterDispatch =
{
    USBAudioFilterCreate,
    NULL,
    NULL,
    NULL
};

static KSPIN_DISPATCH UsbAudioPinDispatch =
{
    USBAudioPinCreate,
    USBAudioPinClose,
    USBAudioPinProcess,
    USBAudioPinReset,
    USBAudioPinSetDataFormat,
    USBAudioPinSetDeviceState,
    NULL,
    NULL,
    NULL,
    NULL
};

NTSTATUS NTAPI FilterAudioVolumeHandler(IN PIRP Irp, IN PKSIDENTIFIER  Request, IN OUT PVOID  Data);
NTSTATUS NTAPI FilterAudioMuteHandler(IN PIRP Irp, IN PKSIDENTIFIER  Request, IN OUT PVOID  Data);
NTSTATUS NTAPI FilterAudioVolumeSupportHandler(IN PIRP Irp, IN PKSIDENTIFIER Request, IN OUT PVOID Data);
NTSTATUS NTAPI FilterAudioMuteSupportHandler(IN PIRP Irp, IN PKSIDENTIFIER Request, IN OUT PVOID Data);

DEFINE_KSPROPERTY_TABLE_AUDIO_VOLUME(FilterAudioVolumePropertySet, FilterAudioVolumeHandler, FilterAudioVolumeSupportHandler);
DEFINE_KSPROPERTY_TABLE_AUDIO_MUTE(FilterAudioMutePropertySet, FilterAudioMuteHandler, FilterAudioMuteSupportHandler);


static KSPROPERTY_SET FilterAudioVolumePropertySetArray[] =
{
    {
        &KSPROPSETID_Audio,
        sizeof(FilterAudioVolumePropertySet) / sizeof(KSPROPERTY_ITEM),
        (const KSPROPERTY_ITEM*)&FilterAudioVolumePropertySet,
        0,
        NULL
    }
};

static KSPROPERTY_SET FilterAudioMutePropertySetArray[] =
{
    {
        &KSPROPSETID_Audio,
        sizeof(FilterAudioMutePropertySet) / sizeof(KSPROPERTY_ITEM),
        (const KSPROPERTY_ITEM*)&FilterAudioMutePropertySet,
        0,
        NULL
    }
};

NTSTATUS
UsbAudioGetSetProperty(
    IN PDEVICE_OBJECT DeviceObject,
    IN UCHAR Request,
    IN USHORT Value,
    IN USHORT Index,
    IN PVOID TransferBuffer,
    IN ULONG TransferBufferLength,
    IN ULONG TransferFlags)
{
    PURB Urb;
    NTSTATUS Status;

    /* allocate urb */
    Urb = AllocFunction(sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST));
    if (!Urb)
    {
        /* no memory */
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* format urb */
    RtlZeroMemory(Urb, sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST));
    if (TransferFlags & USBD_TRANSFER_DIRECTION_IN)
        TransferFlags |= USBD_SHORT_TRANSFER_OK;

    UsbBuildVendorRequest(Urb,
        URB_FUNCTION_CLASS_INTERFACE,
        sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST),
        TransferFlags,
        0,
        Request,
        Value,
        Index,
        TransferBuffer,
        NULL,
        TransferBufferLength,
        NULL);

    /* submit urb */
    Status = SubmitUrbSync(DeviceObject, Urb);


    if (NT_SUCCESS(Status) && !USBD_SUCCESS(Urb->UrbHeader.Status))
        Status = STATUS_IO_DEVICE_ERROR;

    FreeFunction(Urb);
    return Status;
}

static
NTSTATUS
UsbAudioGetSetPropertyTimeout(
    IN PDEVICE_OBJECT DeviceObject,
    IN UCHAR Request,
    IN USHORT Value,
    IN USHORT Index,
    IN PVOID TransferBuffer,
    IN ULONG TransferBufferLength,
    IN ULONG TransferFlags,
    IN ULONG TimeoutMilliseconds)
{
    PURB Urb;
    PIRP Irp;
    PKEVENT Event;
    PIO_STATUS_BLOCK IoStatus;
    PVOID Buffer;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;
    PIO_STACK_LOCATION IoStack;
    BOOLEAN Leak;

    Urb = AllocFunction(sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST));
    Event = AllocFunction(sizeof(KEVENT));
    IoStatus = AllocFunction(sizeof(IO_STATUS_BLOCK));
    Buffer = AllocFunction(max(TransferBufferLength, 1));
    if (!Urb || !Event || !IoStatus || !Buffer)
    {
        if (Urb)
            FreeFunction(Urb);
        if (Event)
            FreeFunction(Event);
        if (IoStatus)
            FreeFunction(IoStatus);
        if (Buffer)
            FreeFunction(Buffer);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (!(TransferFlags & USBD_TRANSFER_DIRECTION_IN))
        RtlCopyMemory(Buffer, TransferBuffer, TransferBufferLength);

    RtlZeroMemory(Urb, sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST));
    if (TransferFlags & USBD_TRANSFER_DIRECTION_IN)
        TransferFlags |= USBD_SHORT_TRANSFER_OK;

    UsbBuildVendorRequest(Urb,
        URB_FUNCTION_CLASS_INTERFACE,
        sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST),
        TransferFlags,
        0,
        Request,
        Value,
        Index,
        Buffer,
        NULL,
        TransferBufferLength,
        NULL);

    KeInitializeEvent(Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_INTERNAL_USB_SUBMIT_URB,
                                        DeviceObject,
                                        NULL,
                                        0,
                                        NULL,
                                        0,
                                        TRUE,
                                        Event,
                                        IoStatus);
    if (!Irp)
    {
        FreeFunction(Urb);
        FreeFunction(Event);
        FreeFunction(IoStatus);
        FreeFunction(Buffer);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->Parameters.Others.Argument1 = Urb;

    Leak = FALSE;
    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        Timeout.QuadPart = -((LONGLONG)TimeoutMilliseconds * 10000);
        Status = KeWaitForSingleObject(Event,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       &Timeout);
        if (Status == STATUS_TIMEOUT)
        {
            IoCancelIrp(Irp);
            Timeout.QuadPart = -5000000LL;
            if (KeWaitForSingleObject(Event,
                                      Executive,
                                      KernelMode,
                                      FALSE,
                                      &Timeout) == STATUS_TIMEOUT)
            {
                Leak = TRUE;
            }
            Status = STATUS_IO_TIMEOUT;
        }
        else
        {
            Status = IoStatus->Status;
        }
    }

    /* an IRP that ignored the cancel still owns the URB and the buffer */
    if (Leak)
        return STATUS_IO_TIMEOUT;

    if (NT_SUCCESS(Status) && !USBD_SUCCESS(Urb->UrbHeader.Status))
        Status = STATUS_IO_DEVICE_ERROR;
    if (NT_SUCCESS(Status) && (TransferFlags & USBD_TRANSFER_DIRECTION_IN))
        RtlCopyMemory(TransferBuffer, Buffer, TransferBufferLength);

    FreeFunction(Urb);
    FreeFunction(Event);
    FreeFunction(IoStatus);
    FreeFunction(Buffer);
    return Status;
}

PNODE_CONTEXT
FindNodeContextWithNode(
    IN PNODE_CONTEXT NodeContext,
    IN ULONG NodeContextCount,
    IN ULONG NodeId)
{
    ULONG Index, NodeIndex;
    for (Index = 0; Index < NodeContextCount; Index++)
    {
        for (NodeIndex = 0; NodeIndex < NodeContext[Index].NodeCount; NodeIndex++)
        {
            if (NodeContext[Index].Nodes[NodeIndex] == NodeId)
            {
                return &NodeContext[Index];
            }
        }
    }
    return NULL;
}

static
ULONG
UsbAudioGetFeatureUnitControlCount(
    IN PUSB_AUDIO_CONTROL_FEATURE_UNIT_DESCRIPTOR FeatureUnitDescriptor);

static
ULONG
UsbAudioFeatureUnitControlCount(
    IN PUSB_AUDIO_CONTROL_FEATURE_UNIT_DESCRIPTOR FeatureUnitDescriptor,
    IN ULONG AudioVersion)
{
    if (!FeatureUnitDescriptor)
        return 0;

    if (AudioVersion >= USB_AUDIO_VERSION_2)
    {
        if (FeatureUnitDescriptor->bLength < 10)
            return 0;

        return (FeatureUnitDescriptor->bLength - 6) / sizeof(ULONG);
    }

    return UsbAudioGetFeatureUnitControlCount(FeatureUnitDescriptor);
}

static
ULONG
UsbAudioGetFeatureUnitControlCount(
    IN PUSB_AUDIO_CONTROL_FEATURE_UNIT_DESCRIPTOR FeatureUnitDescriptor)
{
    ULONG ControlsLength;

    if (!FeatureUnitDescriptor ||
        FeatureUnitDescriptor->bLength <= 7 ||
        FeatureUnitDescriptor->bControlSize == 0)
    {
        return 0;
    }

    ControlsLength = FeatureUnitDescriptor->bLength - 7;
    if (ControlsLength < FeatureUnitDescriptor->bControlSize ||
        ControlsLength % FeatureUnitDescriptor->bControlSize != 0)
        return 0;

    return ControlsLength / FeatureUnitDescriptor->bControlSize;
}

static
ULONG
UsbAudioReadLittleEndian32(
    IN const UCHAR *Buffer);

static
ULONG
UsbAudioGetFeatureUnitControls(
    IN PUSB_COMMON_DESCRIPTOR Descriptor,
    IN ULONG AudioVersion)
{
    ULONG ControlCount;
    ULONG Channel;
    ULONG Controls = 0;

    if (!Descriptor)
        return 0;

    if (AudioVersion >= USB_AUDIO_VERSION_2)
    {
        if (Descriptor->bLength < 6)
            return 0;

        ControlCount = (Descriptor->bLength - 6) / sizeof(ULONG);
        for (Channel = 0; Channel < ControlCount; Channel++)
        {
            Controls |= UacFeatureUnit2Controls(UsbAudioReadLittleEndian32((PUCHAR)Descriptor + 5 +
                                                                           Channel * sizeof(ULONG)));
        }
        return Controls;
    }

    ControlCount = UsbAudioGetFeatureUnitControlCount(
        (PUSB_AUDIO_CONTROL_FEATURE_UNIT_DESCRIPTOR)Descriptor);
    for (Channel = 0; Channel < ControlCount; Channel++)
    {
        Controls |= ((PUSB_AUDIO_CONTROL_FEATURE_UNIT_DESCRIPTOR)Descriptor)->bmaControls[
            Channel * ((PUSB_AUDIO_CONTROL_FEATURE_UNIT_DESCRIPTOR)Descriptor)->bControlSize];
    }

    return Controls;
}

static
ULONG
UsbAudioControlHeaderLength(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    IN PUSB_AUDIO_CONTROL_INTERFACE_HEADER_DESCRIPTOR Header)
{
    ULONG Available;
    ULONG Length;

    if (!ConfigurationDescriptor || !Header || Header->bLength < 3)
        return 0;

    Available = ConfigurationDescriptor->wTotalLength -
                (ULONG)((PUCHAR)Header - (PUCHAR)ConfigurationDescriptor);
    if (Available > ConfigurationDescriptor->wTotalLength)
        return 0;

    /* UAC2 and UAC3 insert bCategory before wTotalLength */
    if (Header->bcdADC >= USB_AUDIO_VERSION_2)
    {
        if (Header->bLength < 9)
            return 0;
        Length = (ULONG)(((PUCHAR)Header)[6] | (((PUCHAR)Header)[7] << 8));
    }
    else
    {
        if (Header->bLength < 8)
            return 0;
        Length = Header->wTotalLength;
    }

    if (Length < Header->bLength || Length > Available)
        return 0;

    return Length;
}

static
PUCHAR
UsbAudioControlEnd(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    IN PUSB_AUDIO_CONTROL_INTERFACE_HEADER_DESCRIPTOR Header)
{
    ULONG Length = UsbAudioControlHeaderLength(ConfigurationDescriptor, Header);

    if (!Length)
        return NULL;

    return (PUCHAR)Header + Length;
}

static
PUSB_COMMON_DESCRIPTOR
UsbAudioFindControlDescriptor(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    IN PUSB_AUDIO_CONTROL_INTERFACE_HEADER_DESCRIPTOR Header)
{
    PUCHAR End = UsbAudioControlEnd(ConfigurationDescriptor, Header);

    if (!End || End <= (PUCHAR)Header + Header->bLength)
        return NULL;

    return USBD_ParseDescriptors(Header,
                                 (ULONG)(End - (PUCHAR)Header),
                                 (PUCHAR)Header + Header->bLength,
                                 USB_AUDIO_CONTROL_TERMINAL_DESCRIPTOR_TYPE);
}

static
PUSB_COMMON_DESCRIPTOR
UsbAudioNextControlDescriptor(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    IN PUSB_AUDIO_CONTROL_INTERFACE_HEADER_DESCRIPTOR Header,
    IN PUSB_COMMON_DESCRIPTOR Current)
{
    PUCHAR End = UsbAudioControlEnd(ConfigurationDescriptor, Header);
    PUCHAR Next;

    if (!End || !Current || Current->bLength < 3)
        return NULL;

    Next = (PUCHAR)Current + Current->bLength;
    if (Next > End - 3)
        return NULL;

    return (PUSB_COMMON_DESCRIPTOR)Next;
}

static
NTSTATUS
UsbAudioGetControlDescriptorBounds(
    IN PDEVICE_EXTENSION DeviceExtension,
    OUT PUCHAR *FirstDescriptor,
    OUT PUCHAR *EndDescriptor)
{
    PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor;
    PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor;
    PUSB_AUDIO_CONTROL_INTERFACE_HEADER_DESCRIPTOR HeaderDescriptor;
    PUCHAR ConfigurationStart;
    PUCHAR ConfigurationEnd;
    ULONG ConfigurationLength;

    *FirstDescriptor = NULL;
    *EndDescriptor = NULL;

    ConfigurationDescriptor = DeviceExtension->ConfigurationDescriptor;
    if (!ConfigurationDescriptor)
        return STATUS_INVALID_DEVICE_STATE;

    ConfigurationLength = ConfigurationDescriptor->wTotalLength;
    if (ConfigurationLength < sizeof(*ConfigurationDescriptor))
        return STATUS_DEVICE_DATA_ERROR;

    ConfigurationStart = (PUCHAR)ConfigurationDescriptor;
    ConfigurationEnd = ConfigurationStart + ConfigurationLength;
    InterfaceDescriptor = USBD_ParseConfigurationDescriptorEx(
        ConfigurationDescriptor,
        ConfigurationDescriptor,
        DeviceExtension->AudioControlInterfaceNumber,
        -1,
        USB_DEVICE_CLASS_AUDIO,
        0x01,
        -1);
    if (!InterfaceDescriptor)
        return STATUS_NOT_FOUND;

    HeaderDescriptor = (PUSB_AUDIO_CONTROL_INTERFACE_HEADER_DESCRIPTOR)
        USBD_ParseDescriptors(ConfigurationDescriptor,
                              ConfigurationLength,
                              InterfaceDescriptor,
                              USB_AUDIO_CONTROL_TERMINAL_DESCRIPTOR_TYPE);
    if (!HeaderDescriptor ||
        (PUCHAR)HeaderDescriptor < ConfigurationStart ||
        (PUCHAR)HeaderDescriptor + 8 > ConfigurationEnd ||
        HeaderDescriptor->bDescriptorSubtype != 0x01 ||
        !UsbAudioControlEnd(ConfigurationDescriptor, HeaderDescriptor))
    {
        return STATUS_DEVICE_DATA_ERROR;
    }

    *FirstDescriptor = (PUCHAR)HeaderDescriptor + HeaderDescriptor->bLength;
    *EndDescriptor = UsbAudioControlEnd(ConfigurationDescriptor, HeaderDescriptor);
    return STATUS_SUCCESS;
}

static
NTSTATUS
UsbAudioFindControlEntity(
    IN PDEVICE_EXTENSION DeviceExtension,
    IN UCHAR EntityId,
    OUT PUSB_COMMON_DESCRIPTOR *EntityDescriptor)
{
    PUSB_COMMON_DESCRIPTOR Descriptor;
    PUCHAR Cursor;
    PUCHAR End;
    NTSTATUS Status;

    *EntityDescriptor = NULL;
    Status = UsbAudioGetControlDescriptorBounds(DeviceExtension, &Cursor, &End);
    if (!NT_SUCCESS(Status))
        return Status;

    while (Cursor < End)
    {
        if ((ULONG)(End - Cursor) < 4)
            return STATUS_DEVICE_DATA_ERROR;

        Descriptor = (PUSB_COMMON_DESCRIPTOR)Cursor;
        if (Descriptor->bLength < 4 || Descriptor->bLength > (ULONG)(End - Cursor))
            return STATUS_DEVICE_DATA_ERROR;

        if (Cursor[1] == USB_AUDIO_CONTROL_TERMINAL_DESCRIPTOR_TYPE &&
            Cursor[2] != 0x01 &&
            Cursor[3] == EntityId)
        {
            *EntityDescriptor = Descriptor;
            return STATUS_SUCCESS;
        }

        Cursor += Descriptor->bLength;
    }

    return STATUS_NOT_FOUND;
}

static
NTSTATUS
UsbAudioCountOutputChannelsForUnit(
    IN PDEVICE_EXTENSION DeviceExtension,
    IN UCHAR EntityId,
    OUT PULONG ChannelCount)
{
    PUSB_COMMON_DESCRIPTOR Descriptor;
    UCHAR Visited[256];
    PUCHAR Bytes;
    ULONG InputPins;
    ULONG Depth;
    NTSTATUS Status;

    *ChannelCount = 0;
    RtlZeroMemory(Visited, sizeof(Visited));

    for (Depth = 0; Depth < RTL_NUMBER_OF(Visited); ++Depth)
    {
        if (!EntityId || Visited[EntityId])
            return STATUS_DEVICE_DATA_ERROR;

        Visited[EntityId] = TRUE;
        Status = UsbAudioFindControlEntity(DeviceExtension,
                                           EntityId,
                                           &Descriptor);
        if (!NT_SUCCESS(Status))
            return Status;

        Bytes = (PUCHAR)Descriptor;
        switch (Bytes[2])
        {
            case USB_AUDIO_INPUT_TERMINAL:
                if (DeviceExtension->AudioVersion >= USB_AUDIO_VERSION_2)
                {
                    if (Descriptor->bLength < 13 || Bytes[8] == 0)
                        return STATUS_DEVICE_DATA_ERROR;
                    *ChannelCount = Bytes[8];
                    return STATUS_SUCCESS;
                }
                if (Descriptor->bLength < 12 || Bytes[7] == 0)
                    return STATUS_DEVICE_DATA_ERROR;
                *ChannelCount = Bytes[7];
                return STATUS_SUCCESS;

            case USB_AUDIO_OUTPUT_TERMINAL:
                if (Descriptor->bLength < 9)
                    return STATUS_DEVICE_DATA_ERROR;
                EntityId = Bytes[7];
                break;

            case USB_AUDIO_MIXER_UNIT:
                if (Descriptor->bLength < 6)
                    return STATUS_DEVICE_DATA_ERROR;
                InputPins = Bytes[4];
                if (!InputPins || Descriptor->bLength <= 5 + InputPins ||
                    Bytes[5 + InputPins] == 0)
                {
                    return STATUS_DEVICE_DATA_ERROR;
                }
                *ChannelCount = Bytes[5 + InputPins];
                return STATUS_SUCCESS;

            case USB_AUDIO_SELECTOR_UNIT:
                if (Descriptor->bLength < 6)
                    return STATUS_DEVICE_DATA_ERROR;
                InputPins = Bytes[4];
                if (!InputPins || Descriptor->bLength < 5 + InputPins)
                    return STATUS_DEVICE_DATA_ERROR;
                EntityId = Bytes[5];
                break;

            case USB_AUDIO_FEATURE_UNIT:
                if (Descriptor->bLength < 6)
                    return STATUS_DEVICE_DATA_ERROR;
                EntityId = Bytes[4];
                break;

            case USB_AUDIO1_PROCESSING_UNIT:
            case USB_AUDIO1_EXTENSION_UNIT:
                if (Descriptor->bLength < 8)
                    return STATUS_DEVICE_DATA_ERROR;
                InputPins = Bytes[6];
                if (!InputPins || Descriptor->bLength <= 7 + InputPins ||
                    Bytes[7 + InputPins] == 0)
                {
                    return STATUS_DEVICE_DATA_ERROR;
                }
                *ChannelCount = Bytes[7 + InputPins];
                return STATUS_SUCCESS;

            default:
                return STATUS_NOT_SUPPORTED;
        }
    }

    return STATUS_DEVICE_DATA_ERROR;
}

static
BOOLEAN
UsbAudioFeatureUnitHasControl(
    IN PUSB_AUDIO_CONTROL_FEATURE_UNIT_DESCRIPTOR FeatureUnitDescriptor,
    IN UCHAR UsbChannel,
    IN UCHAR Control,
    IN ULONG AudioVersion)
{
    ULONG ControlCount;
    ULONG Offset;

    ControlCount = UsbAudioFeatureUnitControlCount(FeatureUnitDescriptor, AudioVersion);
    if (UsbChannel >= ControlCount)
        return FALSE;

    if (AudioVersion >= USB_AUDIO_VERSION_2)
    {
        return (UacFeatureUnit2Controls(UsbAudioReadLittleEndian32((PUCHAR)FeatureUnitDescriptor + 5 +
                                                                   (ULONG)UsbChannel * sizeof(ULONG))) &
                Control) != 0;
    }

    Offset = (ULONG)UsbChannel * FeatureUnitDescriptor->bControlSize;
    return (FeatureUnitDescriptor->bmaControls[Offset] & Control) != 0;
}

static
NTSTATUS
UsbAudioMapFeatureUnitChannel(
    IN PUSB_AUDIO_CONTROL_FEATURE_UNIT_DESCRIPTOR FeatureUnitDescriptor,
    IN ULONG ChannelCount,
    IN LONG KsChannel,
    IN UCHAR Control,
    IN ULONG AudioVersion,
    OUT PUCHAR UsbChannel)
{
    if (KsChannel < 0 || (ULONG)KsChannel >= ChannelCount)
        return STATUS_INVALID_PARAMETER;


    if (UsbAudioFeatureUnitHasControl(FeatureUnitDescriptor,
                                      (UCHAR)KsChannel + 1,
                                      Control,
                                      AudioVersion))
    {
        *UsbChannel = (UCHAR)KsChannel + 1;
        return STATUS_SUCCESS;
    }

    if (UsbAudioFeatureUnitHasControl(FeatureUnitDescriptor, 0, Control, AudioVersion))
    {
        *UsbChannel = 0;
        return STATUS_SUCCESS;
    }

    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
UsbAudioGetFeatureUnitFromRequest(
    IN PIRP Irp,
    IN PKSIDENTIFIER Request,
    OUT PDEVICE_EXTENSION *DeviceExtension,
    OUT PNODE_CONTEXT *FeatureNodeContext,
    OUT PUSB_AUDIO_CONTROL_FEATURE_UNIT_DESCRIPTOR *FeatureUnitDescriptor)
{
    PKSFILTER Filter;
    PFILTER_CONTEXT FilterContext;
    PNODE_CONTEXT NodeContext;
    PKSP_NODE NodeProperty;

    *DeviceExtension = NULL;
    *FeatureNodeContext = NULL;
    *FeatureUnitDescriptor = NULL;

    Filter = KsGetFilterFromIrp(Irp);
    if (!Filter)
        return STATUS_INVALID_PARAMETER;

    FilterContext = (PFILTER_CONTEXT)Filter->Context;
    if (!FilterContext || !FilterContext->DeviceExtension)
        return STATUS_INVALID_PARAMETER;

    *DeviceExtension = FilterContext->DeviceExtension;

    NodeProperty = (PKSP_NODE)Request;
    NodeContext = FindNodeContextWithNode(FilterContext->DeviceExtension->NodeContext,
                                          FilterContext->DeviceExtension->NodeContextCount,
                                          NodeProperty->NodeId);
    if (!NodeContext || !NodeContext->Descriptor)
        return STATUS_INVALID_PARAMETER;

    if (((PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR)NodeContext->Descriptor)->bDescriptorSubtype !=
        USB_AUDIO_FEATURE_UNIT)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *FeatureUnitDescriptor = (PUSB_AUDIO_CONTROL_FEATURE_UNIT_DESCRIPTOR)NodeContext->Descriptor;
    *FeatureNodeContext = NodeContext;
    return STATUS_SUCCESS;
}

static
NTSTATUS
UsbAudioFeatureUnitRequest(
    IN PDEVICE_EXTENSION DeviceExtension,
    IN PUSB_AUDIO_CONTROL_FEATURE_UNIT_DESCRIPTOR FeatureUnitDescriptor,
    IN UCHAR Request,
    IN USHORT Control,
    IN UCHAR UsbChannel,
    IN PVOID Data,
    IN ULONG DataLength,
    IN ULONG TransferFlags)
{
    USHORT Value;
    USHORT Index;
    NTSTATUS Status;

    Value = Control | UsbChannel;
    Index = ((USHORT)FeatureUnitDescriptor->bUnitID << 8) |
            DeviceExtension->AudioControlInterfaceNumber;

    if (DeviceExtension->AudioVersion >= USB_AUDIO_VERSION_2 &&
        (Request == USB_AUDIO_GET_CUR || Request == USB_AUDIO_SET_CUR))
    {
        Request = USB_AUDIO2_CUR;
    }

    Status = UsbAudioGetSetPropertyTimeout(DeviceExtension->LowerDevice,
                                           Request,
                                           Value,
                                           Index,
                                           Data,
                                           DataLength,
                                           TransferFlags,
                                           1500);
    return Status;
}

static
NTSTATUS
UsbAudioGetVolumeRange(
    IN PDEVICE_EXTENSION DeviceExtension,
    IN PUSB_AUDIO_CONTROL_FEATURE_UNIT_DESCRIPTOR FeatureUnitDescriptor,
    IN UCHAR UsbChannel,
    OUT PKSPROPERTY_STEPPING_LONG Range)
{
    SHORT Minimum;
    SHORT Maximum;
    SHORT Resolution;
    NTSTATUS Status;

    if (DeviceExtension->AudioVersion >= USB_AUDIO_VERSION_2)
    {
        UCHAR Data[2 + 6];

        Status = UsbAudioFeatureUnitRequest(DeviceExtension,
                                            FeatureUnitDescriptor,
                                            USB_AUDIO2_GET_RANGE,
                                            USB_AUDIO_VOLUME_CONTROL,
                                            UsbChannel,
                                            Data,
                                            sizeof(Data),
                                            USBD_TRANSFER_DIRECTION_IN);
        if (!NT_SUCCESS(Status))
            return Status;

        if ((Data[0] | (Data[1] << 8)) == 0)
            return STATUS_DEVICE_DATA_ERROR;

        Minimum = (SHORT)(Data[2] | (Data[3] << 8));
        Maximum = (SHORT)(Data[4] | (Data[5] << 8));
        Resolution = (SHORT)(Data[6] | (Data[7] << 8));
    }
    else
    {
        Status = UsbAudioFeatureUnitRequest(DeviceExtension,
                                            FeatureUnitDescriptor,
                                            USB_AUDIO_GET_MIN,
                                            USB_AUDIO_VOLUME_CONTROL,
                                            UsbChannel,
                                            &Minimum,
                                            sizeof(Minimum),
                                            USBD_TRANSFER_DIRECTION_IN);
        if (!NT_SUCCESS(Status))
            return Status;

        Status = UsbAudioFeatureUnitRequest(DeviceExtension,
                                            FeatureUnitDescriptor,
                                            USB_AUDIO_GET_MAX,
                                            USB_AUDIO_VOLUME_CONTROL,
                                            UsbChannel,
                                            &Maximum,
                                            sizeof(Maximum),
                                            USBD_TRANSFER_DIRECTION_IN);
        if (!NT_SUCCESS(Status))
            return Status;

        Status = UsbAudioFeatureUnitRequest(DeviceExtension,
                                            FeatureUnitDescriptor,
                                            USB_AUDIO_GET_RES,
                                            USB_AUDIO_VOLUME_CONTROL,
                                            UsbChannel,
                                            &Resolution,
                                            sizeof(Resolution),
                                            USBD_TRANSFER_DIRECTION_IN);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    if (Minimum > Maximum || Resolution <= 0)
        return STATUS_DEVICE_DATA_ERROR;

    Range->SteppingDelta = (ULONG)Resolution * 256;
    Range->Reserved = 0;
    Range->Bounds.SignedMinimum = (LONG)Minimum * 256;
    Range->Bounds.SignedMaximum = (LONG)Maximum * 256;
    return STATUS_SUCCESS;
}

static
NTSTATUS
UsbAudioInitializeFeatureUnitCache(
    IN PKSDEVICE Device,
    IN PDEVICE_EXTENSION DeviceExtension,
    IN PNODE_CONTEXT NodeContext,
    IN PUSB_AUDIO_CONTROL_FEATURE_UNIT_DESCRIPTOR FeatureUnitDescriptor)
{
    PUSBAUDIO_VOLUME_RANGE_CACHE VolumeRanges;
    ULONG ChannelCount;
    ULONG Channel;
    ULONG PreviousChannel;
    UCHAR UsbChannel;
    NTSTATUS Status;

    Status = UsbAudioCountOutputChannelsForUnit(DeviceExtension,
                                                FeatureUnitDescriptor->bSourceID,
                                                &ChannelCount);
    if (!NT_SUCCESS(Status))
        return Status;

    VolumeRanges = AllocFunction(ChannelCount * sizeof(*VolumeRanges));
    if (!VolumeRanges)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = KsAddItemToObjectBag(Device->Bag, VolumeRanges, ExFreePool);
    if (!NT_SUCCESS(Status))
    {
        FreeFunction(VolumeRanges);
        return Status;
    }

    NodeContext->FeatureChannelCount = ChannelCount;
    NodeContext->VolumeRanges = VolumeRanges;

    for (Channel = 0; Channel < ChannelCount; ++Channel)
    {
        Status = UsbAudioMapFeatureUnitChannel(FeatureUnitDescriptor,
                                               ChannelCount,
                                               Channel,
                                               USB_AUDIO_FU_VOLUME,
                                               DeviceExtension->AudioVersion,
                                               &UsbChannel);
        if (!NT_SUCCESS(Status))
            continue;

        VolumeRanges[Channel].UsbChannel = UsbChannel;
        for (PreviousChannel = 0; PreviousChannel < Channel; ++PreviousChannel)
        {
            if (VolumeRanges[PreviousChannel].Valid &&
                VolumeRanges[PreviousChannel].UsbChannel == UsbChannel)
            {
                VolumeRanges[Channel].Range = VolumeRanges[PreviousChannel].Range;
                VolumeRanges[Channel].Valid = TRUE;
                break;
            }
        }
        if (VolumeRanges[Channel].Valid)
            continue;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
UsbAudioPropertyBasicSupport(
    IN PIRP Irp,
    IN PKSIDENTIFIER Request,
    IN OUT PVOID Data,
    IN ULONG ValueType,
    IN BOOLEAN VolumeProperty)
{
    PIO_STACK_LOCATION IoStack;
    ULONG OutputBufferLength;
    ULONG AccessFlags;
    ULONG DescriptionSize;
    PUSB_AUDIO_CONTROL_FEATURE_UNIT_DESCRIPTOR FeatureUnitDescriptor;
    PDEVICE_EXTENSION DeviceExtension;
    PNODE_CONTEXT NodeContext;
    PKSPROPERTY_DESCRIPTION Description;
    PKSPROPERTY_MEMBERSHEADER Members;
    PKSPROPERTY_STEPPING_LONG Range;
    ULONG ChannelCount;
    ULONG Channel;
    ULONG Uniform;
    UCHAR Control;
    UCHAR UsbChannel;
    NTSTATUS Status;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    OutputBufferLength = IoStack->Parameters.DeviceIoControl.OutputBufferLength;

    if (OutputBufferLength < sizeof(ULONG))
        return STATUS_BUFFER_TOO_SMALL;

    AccessFlags = KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT;

    if (OutputBufferLength < sizeof(KSPROPERTY_DESCRIPTION))
    {
        *(PULONG)Data = AccessFlags;
        Irp->IoStatus.Information = sizeof(ULONG);
        return STATUS_SUCCESS;
    }

    Status = UsbAudioGetFeatureUnitFromRequest(Irp,
                                               Request,
                                               &DeviceExtension,
                                               &NodeContext,
                                               &FeatureUnitDescriptor);
    if (!NT_SUCCESS(Status))
        return Status;

    ChannelCount = NodeContext->FeatureChannelCount;
    if (!ChannelCount)
        return STATUS_DEVICE_DATA_ERROR;

    DescriptionSize = sizeof(KSPROPERTY_DESCRIPTION) +
                      sizeof(KSPROPERTY_MEMBERSHEADER) +
                      ChannelCount * sizeof(KSPROPERTY_STEPPING_LONG);

    RtlZeroMemory(Data, OutputBufferLength);

    Description = (PKSPROPERTY_DESCRIPTION)Data;
    Description->AccessFlags = AccessFlags;
    Description->DescriptionSize = DescriptionSize;
    Description->PropTypeSet.Set = KSPROPTYPESETID_General;
    Description->PropTypeSet.Id = ValueType;
    Description->PropTypeSet.Flags = 0;
    Description->MembersListCount = 1;
    Description->Reserved = 0;

    Irp->IoStatus.Information = sizeof(KSPROPERTY_DESCRIPTION);

    if (OutputBufferLength < DescriptionSize)
        return STATUS_SUCCESS;

    Members = (PKSPROPERTY_MEMBERSHEADER)(Description + 1);
    Members->MembersFlags = KSPROPERTY_MEMBER_STEPPEDRANGES;
    Members->MembersSize = sizeof(KSPROPERTY_STEPPING_LONG);
    Members->MembersCount = ChannelCount;
    Members->Flags = KSPROPERTY_MEMBER_FLAG_BASICSUPPORT_MULTICHANNEL;
    Range = (PKSPROPERTY_STEPPING_LONG)(Members + 1);

    Control = VolumeProperty ? USB_AUDIO_FU_VOLUME : USB_AUDIO_FU_MUTE;
    Uniform = TRUE;
    for (Channel = 0; Channel < ChannelCount; ++Channel)
    {
        Status = UsbAudioMapFeatureUnitChannel(FeatureUnitDescriptor,
                                               ChannelCount,
                                               Channel,
                                               Control,
                                               DeviceExtension->AudioVersion,
                                               &UsbChannel);
        if (Status == STATUS_NOT_SUPPORTED)
        {
            RtlZeroMemory(&Range[Channel], sizeof(Range[Channel]));
            Uniform = FALSE;
            continue;
        }
        if (!NT_SUCCESS(Status))
            return Status;

        if (UsbChannel != 0)
            Uniform = FALSE;

        if (VolumeProperty)
        {
            if (Channel < NodeContext->FeatureChannelCount &&
                NodeContext->VolumeRanges &&
                NodeContext->VolumeRanges[Channel].Valid)
            {
                Range[Channel] = NodeContext->VolumeRanges[Channel].Range;
            }
            else
            {
                Status = UsbAudioGetVolumeRange(DeviceExtension,
                                                FeatureUnitDescriptor,
                                                UsbChannel,
                                                &Range[Channel]);
                if (!NT_SUCCESS(Status))
                    return Status;
            }
        }
        else
        {
            Range[Channel].SteppingDelta = 1;
            Range[Channel].Reserved = 0;
            Range[Channel].Bounds.SignedMinimum = FALSE;
            Range[Channel].Bounds.SignedMaximum = TRUE;
        }
    }

    if (Uniform)
        Members->Flags |= KSPROPERTY_MEMBER_FLAG_BASICSUPPORT_UNIFORM;

    Irp->IoStatus.Information = DescriptionSize;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FilterAudioMuteSupportHandler(
    IN PIRP Irp,
    IN PKSIDENTIFIER Request,
    IN OUT PVOID Data)
{
    return UsbAudioPropertyBasicSupport(Irp, Request, Data, VT_BOOL, FALSE);
}

NTSTATUS
NTAPI
FilterAudioVolumeSupportHandler(
    IN PIRP Irp,
    IN PKSIDENTIFIER Request,
    IN OUT PVOID Data)
{
    return UsbAudioPropertyBasicSupport(Irp, Request, Data, VT_I4, TRUE);
}


NTSTATUS
NTAPI
FilterAudioMuteHandler(
    IN PIRP Irp,
    IN PKSIDENTIFIER  Request,
    IN OUT PVOID  Data)
{
    PKSNODEPROPERTY_AUDIO_CHANNEL Property;
    PDEVICE_EXTENSION DeviceExtension;
    PNODE_CONTEXT NodeContext;
    PUSB_AUDIO_CONTROL_FEATURE_UNIT_DESCRIPTOR FeatureUnitDescriptor;
    UCHAR UsbChannel;
    UCHAR Mute;
    NTSTATUS Status;

    Property = (PKSNODEPROPERTY_AUDIO_CHANNEL)Request;
    Status = UsbAudioGetFeatureUnitFromRequest(Irp,
                                               Request,
                                               &DeviceExtension,
                                               &NodeContext,
                                               &FeatureUnitDescriptor);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = UsbAudioMapFeatureUnitChannel(FeatureUnitDescriptor,
                                           NodeContext->FeatureChannelCount,
                                           Property->Channel,
                                           USB_AUDIO_FU_MUTE,
                                           DeviceExtension->AudioVersion,
                                           &UsbChannel);
    if (!NT_SUCCESS(Status))
        return Status;


    if (Property->NodeProperty.Property.Flags & KSPROPERTY_TYPE_GET)
    {
        Status = UsbAudioFeatureUnitRequest(DeviceExtension,
                                            FeatureUnitDescriptor,
                                            USB_AUDIO_GET_CUR,
                                            USB_AUDIO_MUTE_CONTROL,
                                            UsbChannel,
                                            &Mute,
                                            sizeof(Mute),
                                            USBD_TRANSFER_DIRECTION_IN);
        if (NT_SUCCESS(Status))
        {
            *(PBOOL)Data = Mute != 0;
            Irp->IoStatus.Information = sizeof(BOOL);
        }
        return Status;
    }

    Mute = *(PBOOL)Data ? TRUE : FALSE;

    Status = UsbAudioFeatureUnitRequest(DeviceExtension,
                                        FeatureUnitDescriptor,
                                        USB_AUDIO_SET_CUR,
                                        USB_AUDIO_MUTE_CONTROL,
                                        UsbChannel,
                                        &Mute,
                                        sizeof(Mute),
                                        USBD_TRANSFER_DIRECTION_OUT);
    if (NT_SUCCESS(Status))
        Irp->IoStatus.Information = sizeof(BOOL);
    return Status;
}

NTSTATUS
NTAPI
FilterAudioVolumeHandler(
    IN PIRP Irp,
    IN PKSIDENTIFIER  Request,
    IN OUT PVOID  Data)
{
    PKSNODEPROPERTY_AUDIO_CHANNEL Property;
    PDEVICE_EXTENSION DeviceExtension;
    PNODE_CONTEXT NodeContext;
    PUSB_AUDIO_CONTROL_FEATURE_UNIT_DESCRIPTOR FeatureUnitDescriptor;
    KSPROPERTY_STEPPING_LONG Range;
    UCHAR UsbChannel;
    SHORT RawValue;
    LONG Value;
    NTSTATUS Status;

    Property = (PKSNODEPROPERTY_AUDIO_CHANNEL)Request;
    Status = UsbAudioGetFeatureUnitFromRequest(Irp,
                                               Request,
                                               &DeviceExtension,
                                               &NodeContext,
                                               &FeatureUnitDescriptor);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = UsbAudioMapFeatureUnitChannel(FeatureUnitDescriptor,
                                           NodeContext->FeatureChannelCount,
                                           Property->Channel,
                                           USB_AUDIO_FU_VOLUME,
                                           DeviceExtension->AudioVersion,
                                           &UsbChannel);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Property->NodeProperty.Property.Flags & KSPROPERTY_TYPE_GET)
    {
        Status = UsbAudioFeatureUnitRequest(DeviceExtension,
                                            FeatureUnitDescriptor,
                                            USB_AUDIO_GET_CUR,
                                            USB_AUDIO_VOLUME_CONTROL,
                                            UsbChannel,
                                            &RawValue,
                                            sizeof(RawValue),
                                            USBD_TRANSFER_DIRECTION_IN);
        if (NT_SUCCESS(Status))
        {
            *(PLONG)Data = (LONG)RawValue * 256;
            Irp->IoStatus.Information = sizeof(LONG);
        }
        return Status;
    }

    if ((ULONG)Property->Channel < NodeContext->FeatureChannelCount &&
        NodeContext->VolumeRanges &&
        NodeContext->VolumeRanges[Property->Channel].Valid)
    {
        Range = NodeContext->VolumeRanges[Property->Channel].Range;
    }
    else
    {
        Status = UsbAudioGetVolumeRange(DeviceExtension,
                                        FeatureUnitDescriptor,
                                        UsbChannel,
                                        &Range);
        if (!NT_SUCCESS(Status))
            return Status;

        if ((ULONG)Property->Channel < NodeContext->FeatureChannelCount &&
            NodeContext->VolumeRanges)
        {
            NodeContext->VolumeRanges[Property->Channel].Range = Range;
            NodeContext->VolumeRanges[Property->Channel].UsbChannel = UsbChannel;
            NodeContext->VolumeRanges[Property->Channel].Valid = TRUE;
        }
    }

    Value = *(PLONG)Data;
    if (Value < Range.Bounds.SignedMinimum)
        Value = Range.Bounds.SignedMinimum;
    else if (Value > Range.Bounds.SignedMaximum)
        Value = Range.Bounds.SignedMaximum;

    Value = Range.Bounds.SignedMinimum +
            ((Value - Range.Bounds.SignedMinimum + Range.SteppingDelta / 2) /
             Range.SteppingDelta) * Range.SteppingDelta;
    if (Value > Range.Bounds.SignedMaximum)
        Value = Range.Bounds.SignedMaximum;

    RawValue = (SHORT)(Value / 256);

    Status = UsbAudioFeatureUnitRequest(DeviceExtension,
                                        FeatureUnitDescriptor,
                                        USB_AUDIO_SET_CUR,
                                        USB_AUDIO_VOLUME_CONTROL,
                                        UsbChannel,
                                        &RawValue,
                                        sizeof(RawValue),
                                        USBD_TRANSFER_DIRECTION_OUT);
    if (NT_SUCCESS(Status))
        Irp->IoStatus.Information = sizeof(LONG);
    return Status;
}


ULONG
CountTopologyComponents(
    IN PDEVICE_EXTENSION DeviceExtension,
    OUT PULONG OutDescriptorCount)
{
    PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor;
    PUSB_INTERFACE_DESCRIPTOR Descriptor;
    PUSB_AUDIO_CONTROL_INTERFACE_HEADER_DESCRIPTOR InterfaceHeaderDescriptor;
    PUSB_COMMON_DESCRIPTOR CommonDescriptor;
    PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR InputTerminalDescriptor;
    PUSB_AUDIO_CONTROL_MIXER_UNIT_DESCRIPTOR MixerUnitDescriptor;
    ULONG NodeCount = 0;
    ULONG DescriptorCount = 0;
    UCHAR Value;

    ConfigurationDescriptor = DeviceExtension->ConfigurationDescriptor;
    for (Descriptor = USBD_ParseConfigurationDescriptorEx(ConfigurationDescriptor, ConfigurationDescriptor, -1, -1, USB_DEVICE_CLASS_AUDIO, -1, -1);
    Descriptor != NULL;
        Descriptor = USBD_ParseConfigurationDescriptorEx(ConfigurationDescriptor, (PVOID)((ULONG_PTR)Descriptor + Descriptor->bLength), -1, -1, USB_DEVICE_CLASS_AUDIO, -1, -1))
    {
        if (Descriptor->bInterfaceSubClass == 0x01) /* AUDIO_CONTROL */
        {
            InterfaceHeaderDescriptor = (PUSB_AUDIO_CONTROL_INTERFACE_HEADER_DESCRIPTOR)USBD_ParseDescriptors(ConfigurationDescriptor, ConfigurationDescriptor->wTotalLength, Descriptor, USB_AUDIO_CONTROL_TERMINAL_DESCRIPTOR_TYPE);
            if (InterfaceHeaderDescriptor != NULL)
            {
                CommonDescriptor = UsbAudioFindControlDescriptor(ConfigurationDescriptor, InterfaceHeaderDescriptor);
                while (CommonDescriptor)
                {
                    InputTerminalDescriptor = (PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR)CommonDescriptor;
                    if (InputTerminalDescriptor->bDescriptorSubtype == 0x02 /* INPUT TERMINAL*/ || InputTerminalDescriptor->bDescriptorSubtype == 0x03 /* OUTPUT_TERMINAL*/)
                    {
                        NodeCount++;
                        DescriptorCount++;
                    }
                    else if (InputTerminalDescriptor->bDescriptorSubtype == 0x06 /* FEATURE_UNIT*/)
                    {
                        DescriptorCount++;

                        Value = UsbAudioGetFeatureUnitControls(CommonDescriptor,
                                                               DeviceExtension->AudioVersion);

                        if (Value & 0x01) /* MUTE*/
                            NodeCount++;
                        if (Value & 0x02) /* VOLUME */
                            NodeCount++;
                        if (Value & 0x04) /* BASS */
                            NodeCount++;
                        if (Value & 0x08) /* MID */
                            NodeCount++;
                        if (Value & 0x10) /* TREBLE */
                            NodeCount++;
                        if (Value & 0x20) /* GRAPHIC EQUALIZER */
                            NodeCount++;
                        if (Value & 0x40) /* AUTOMATIC GAIN */
                            NodeCount++;
                        if (Value & 0x80) /* DELAY */
                            NodeCount++;
                    }
                    else if (InputTerminalDescriptor->bDescriptorSubtype == 0x04 /* MIXER_UNIT */)
                    {
                        MixerUnitDescriptor = (PUSB_AUDIO_CONTROL_MIXER_UNIT_DESCRIPTOR)InputTerminalDescriptor;
                        DescriptorCount++;
                        NodeCount += MixerUnitDescriptor->bNrInPins + 1; /* KSNODETYPE_SUPERMIX for each source pin and KSNODETYPE_SUM for target */
                    }
                    else if (InputTerminalDescriptor->bDescriptorSubtype == 0x05 /* SELECTOR_UNIT */)
                    {
                        DescriptorCount++;
                        NodeCount++;
                    }
                    else if (DeviceExtension->AudioVersion == USB_AUDIO_VERSION_1 &&
                             (InputTerminalDescriptor->bDescriptorSubtype == USB_AUDIO1_PROCESSING_UNIT ||
                              InputTerminalDescriptor->bDescriptorSubtype == USB_AUDIO1_EXTENSION_UNIT))
                    {
                        /* Keep unsupported UAC1 units in the entity chain as transparent nodes. */
                        DescriptorCount++;
                    }
                    else
                    {
                        DPRINT1("BuildUSBAudioFilterTopology: unknown descriptor subtype %x\n",
                                InputTerminalDescriptor->bDescriptorSubtype);
                    }
                    CommonDescriptor = UsbAudioNextControlDescriptor(ConfigurationDescriptor, InterfaceHeaderDescriptor, CommonDescriptor);
                }
            }
        }
    }
    *OutDescriptorCount = DescriptorCount;
    return NodeCount;
}

PNODE_CONTEXT
FindNodeContextWithId(
    IN PNODE_CONTEXT NodeContext,
    IN ULONG NodeContextCount,
    IN UCHAR TerminalId)
{
    ULONG Index;
    PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR TerminalDescriptor;

    for (Index = 0; Index < NodeContextCount; Index++)
    {
        if (!NodeContext[Index].Descriptor)
            continue;

        TerminalDescriptor = (PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR)NodeContext[Index].Descriptor;
        if (TerminalDescriptor->bTerminalID == TerminalId)
            return &NodeContext[Index];
    }
    return NULL;
}

static BOOLEAN
UsbAudioResolveLastNodeForId(
    IN PNODE_CONTEXT NodeContext,
    IN ULONG NodeContextCount,
    IN UCHAR TerminalId,
    OUT PULONG NodeId,
    IN ULONG RecursionDepth)
{
    PNODE_CONTEXT CurrentNodeContext;
    PUSB_COMMON_DESCRIPTOR CommonDescriptor;
    PUSB_AUDIO_CONTROL_FEATURE_UNIT_DESCRIPTOR FeatureUnitDescriptor;
    PUCHAR DescriptorBytes;
    UCHAR SourceId;

    if (RecursionDepth >= NodeContextCount)
        return FALSE;

    CurrentNodeContext = FindNodeContextWithId(NodeContext,
                                               NodeContextCount,
                                               TerminalId);
    if (!CurrentNodeContext || !CurrentNodeContext->Descriptor)
        return FALSE;

    if (CurrentNodeContext->NodeCount != 0)
    {
        *NodeId = CurrentNodeContext->Nodes[CurrentNodeContext->NodeCount - 1];
        return TRUE;
    }

    CommonDescriptor = CurrentNodeContext->Descriptor;
    DescriptorBytes = (PUCHAR)CommonDescriptor;
    if (DescriptorBytes[2] == USB_AUDIO_FEATURE_UNIT)
    {
        if (CommonDescriptor->bLength < 6)
            return FALSE;
        FeatureUnitDescriptor = (PUSB_AUDIO_CONTROL_FEATURE_UNIT_DESCRIPTOR)CommonDescriptor;
        SourceId = FeatureUnitDescriptor->bSourceID;
    }
    else if (DescriptorBytes[2] == USB_AUDIO1_PROCESSING_UNIT ||
             DescriptorBytes[2] == USB_AUDIO1_EXTENSION_UNIT)
    {
        if (CommonDescriptor->bLength < 8 || DescriptorBytes[6] == 0)
            return FALSE;
        SourceId = DescriptorBytes[7];
    }
    else
    {
        return FALSE;
    }

    return UsbAudioResolveLastNodeForId(NodeContext,
                                        NodeContextCount,
                                        SourceId,
                                        NodeId,
                                        RecursionDepth + 1);
}

static
ULONG
UsbAudioCountRenderBridgePins(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor)
{
    ULONG Index;
    ULONG Count = 0;
    ULONG NonStreamingTerminalDescriptorCount;
    ULONG TotalTerminalDescriptorCount;
    ULONG StreamingTerminalDescriptorCount;
    PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR TerminalDescriptor;

    CountTerminalUnits(ConfigurationDescriptor,
                       &NonStreamingTerminalDescriptorCount,
                       &TotalTerminalDescriptorCount);
    if (NonStreamingTerminalDescriptorCount > TotalTerminalDescriptorCount)
        return 0;

    StreamingTerminalDescriptorCount = TotalTerminalDescriptorCount -
                                       NonStreamingTerminalDescriptorCount;

    for (Index = 0; Index < StreamingTerminalDescriptorCount; Index++)
    {
        TerminalDescriptor = UsbAudioGetStreamingTerminalDescriptorByIndex(ConfigurationDescriptor,
                                                                          Index);
        if (TerminalDescriptor &&
            TerminalDescriptor->bDescriptorSubtype == USB_AUDIO_INPUT_TERMINAL)
        {
            Count++;
        }
    }

    return Count;
}

static
BOOLEAN
UsbAudioGetRenderBridgePinId(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    IN UCHAR TerminalId,
    IN ULONG StreamingTerminalDescriptorCount,
    OUT PULONG PinId)
{
    ULONG Index;
    ULONG BridgeIndex = 0;
    ULONG NonStreamingTerminalDescriptorCount;
    ULONG TotalTerminalDescriptorCount;
    PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR TerminalDescriptor;

    CountTerminalUnits(ConfigurationDescriptor,
                       &NonStreamingTerminalDescriptorCount,
                       &TotalTerminalDescriptorCount);
    if (NonStreamingTerminalDescriptorCount > TotalTerminalDescriptorCount)
        return FALSE;

    for (Index = 0; Index < TotalTerminalDescriptorCount - NonStreamingTerminalDescriptorCount; Index++)
    {
        TerminalDescriptor = UsbAudioGetStreamingTerminalDescriptorByIndex(ConfigurationDescriptor,
                                                                          Index);
        if (!TerminalDescriptor ||
            TerminalDescriptor->bDescriptorSubtype != USB_AUDIO_INPUT_TERMINAL)
        {
            continue;
        }

        if (TerminalDescriptor->bTerminalID == TerminalId)
        {
            *PinId = StreamingTerminalDescriptorCount + BridgeIndex;
            return TRUE;
        }

        BridgeIndex++;
    }

    return FALSE;
}

static
PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR
UsbAudioGetRenderBridgeTerminalDescriptorByIndex(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    IN ULONG BridgeIndex)
{
    ULONG Index;
    ULONG CurrentBridgeIndex = 0;
    ULONG NonStreamingTerminalDescriptorCount;
    ULONG TotalTerminalDescriptorCount;
    PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR TerminalDescriptor;

    CountTerminalUnits(ConfigurationDescriptor,
                       &NonStreamingTerminalDescriptorCount,
                       &TotalTerminalDescriptorCount);
    if (NonStreamingTerminalDescriptorCount > TotalTerminalDescriptorCount)
        return NULL;

    for (Index = 0; Index < TotalTerminalDescriptorCount - NonStreamingTerminalDescriptorCount; Index++)
    {
        TerminalDescriptor = UsbAudioGetStreamingTerminalDescriptorByIndex(ConfigurationDescriptor,
                                                                          Index);
        if (!TerminalDescriptor ||
            TerminalDescriptor->bDescriptorSubtype != USB_AUDIO_INPUT_TERMINAL)
        {
            continue;
        }

        if (CurrentBridgeIndex == BridgeIndex)
            return TerminalDescriptor;

        CurrentBridgeIndex++;
    }

    return NULL;
}

static
NTSTATUS
UsbAudioAddConnectionCount(
    IN OUT PULONG ConnectionsCount,
    IN ULONG AdditionalCount)
{
    if (*ConnectionsCount > MAXULONG - AdditionalCount)
        return STATUS_INTEGER_OVERFLOW;

    *ConnectionsCount += AdditionalCount;
    return STATUS_SUCCESS;
}

static
NTSTATUS
UsbAudioCountSourceToNodeConnection(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    IN OUT PULONG ConnectionsCount,
    IN UCHAR SourceId,
    IN ULONG StreamingTerminalDescriptorCount)
{
    ULONG BridgePinId;

    return UsbAudioAddConnectionCount(ConnectionsCount,
                                      UsbAudioGetRenderBridgePinId(ConfigurationDescriptor,
                                                                   SourceId,
                                                                   StreamingTerminalDescriptorCount,
                                                                   &BridgePinId) ? 2 : 1);
}

static
NTSTATUS
UsbAudioAppendTopologyConnection(
    IN OUT PKSTOPOLOGY_CONNECTION Connections,
    IN ULONG MaxConnections,
    IN OUT PULONG ConnectionsCount,
    IN ULONG FromNode,
    IN ULONG FromNodePin,
    IN ULONG ToNode,
    IN ULONG ToNodePin)
{
    if (*ConnectionsCount >= MaxConnections)
    {
        DPRINT1("BuildUSBAudioFilterTopology: connection allocation exceeded %lu/%lu\n",
                *ConnectionsCount,
                MaxConnections);
        return STATUS_BUFFER_TOO_SMALL;
    }

    Connections[*ConnectionsCount].FromNode = FromNode;
    Connections[*ConnectionsCount].FromNodePin = FromNodePin;
    Connections[*ConnectionsCount].ToNode = ToNode;
    Connections[*ConnectionsCount].ToNodePin = ToNodePin;
    (*ConnectionsCount)++;

    return STATUS_SUCCESS;
}

static
NTSTATUS
UsbAudioAddSourceToNodeConnection(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    IN OUT PKSTOPOLOGY_CONNECTION Connections,
    IN ULONG MaxConnections,
    IN OUT PULONG ConnectionsCount,
    IN UCHAR SourceId,
    IN ULONG SourceNode,
    IN ULONG TargetNode,
    IN ULONG TargetNodePin,
    IN ULONG StreamingTerminalDescriptorCount)
{
    ULONG BridgePinId;
    NTSTATUS Status;

    if (UsbAudioGetRenderBridgePinId(ConfigurationDescriptor,
                                     SourceId,
                                     StreamingTerminalDescriptorCount,
                                     &BridgePinId))
    {
        Status = UsbAudioAppendTopologyConnection(Connections,
                                                  MaxConnections,
                                                  ConnectionsCount,
                                                  SourceNode,
                                                  0,
                                                  KSFILTER_NODE,
                                                  BridgePinId);
        if (!NT_SUCCESS(Status))
            return Status;

        return UsbAudioAppendTopologyConnection(Connections,
                                                MaxConnections,
                                                ConnectionsCount,
                                                KSFILTER_NODE,
                                                BridgePinId,
                                                TargetNode,
                                                TargetNodePin);
    }

    return UsbAudioAppendTopologyConnection(Connections,
                                            MaxConnections,
                                            ConnectionsCount,
                                            SourceNode,
                                            0,
                                            TargetNode,
                                            TargetNodePin);
}

static
NTSTATUS
UsbAudioCountTopologyConnections(
    IN PDEVICE_EXTENSION DeviceExtension,
    IN PNODE_CONTEXT NodeContext,
    IN ULONG NodeContextCount,
    IN ULONG StreamingTerminalDescriptorCount,
    OUT PULONG OutConnectionsCount)
{
    PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor;
    PUSB_INTERFACE_DESCRIPTOR Descriptor;
    PUSB_AUDIO_CONTROL_INTERFACE_HEADER_DESCRIPTOR InterfaceHeaderDescriptor;
    PUSB_COMMON_DESCRIPTOR CommonDescriptor;
    PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR InputTerminalDescriptor;
    PUSB_AUDIO_CONTROL_FEATURE_UNIT_DESCRIPTOR FeatureUnitDescriptor;
    PUSB_AUDIO_CONTROL_MIXER_UNIT_DESCRIPTOR MixerUnitDescriptor;
    PUSB_AUDIO_CONTROL_OUTPUT_TERMINAL_DESCRIPTOR OutputTerminalDescriptor;
    PUSB_AUDIO_CONTROL_SELECTOR_UNIT_DESCRIPTOR SelectorUnitDescriptor;
    ULONG DescriptorCount = 0;
    ULONG Index;
    ULONG SourceNode;
    UCHAR Value;
    NTSTATUS Status;

    *OutConnectionsCount = 0;
    ConfigurationDescriptor = DeviceExtension->ConfigurationDescriptor;

    for (Descriptor = USBD_ParseConfigurationDescriptorEx(ConfigurationDescriptor, ConfigurationDescriptor, -1, -1, USB_DEVICE_CLASS_AUDIO, -1, -1);
    Descriptor != NULL;
        Descriptor = USBD_ParseConfigurationDescriptorEx(ConfigurationDescriptor, (PVOID)((ULONG_PTR)Descriptor + Descriptor->bLength), -1, -1, USB_DEVICE_CLASS_AUDIO, -1, -1))
    {
        if (Descriptor->bInterfaceSubClass != 0x01) /* AUDIO_CONTROL */
            continue;

        InterfaceHeaderDescriptor = (PUSB_AUDIO_CONTROL_INTERFACE_HEADER_DESCRIPTOR)USBD_ParseDescriptors(ConfigurationDescriptor, ConfigurationDescriptor->wTotalLength, Descriptor, USB_AUDIO_CONTROL_TERMINAL_DESCRIPTOR_TYPE);
        if (InterfaceHeaderDescriptor == NULL)
            continue;

        CommonDescriptor = UsbAudioFindControlDescriptor(ConfigurationDescriptor, InterfaceHeaderDescriptor);
        while (CommonDescriptor)
        {
            InputTerminalDescriptor = (PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR)CommonDescriptor;
            if (InputTerminalDescriptor->bDescriptorSubtype == USB_AUDIO_INPUT_TERMINAL)
            {
                if (DescriptorCount >= NodeContextCount)
                    return STATUS_INVALID_DEVICE_REQUEST;

                Status = UsbAudioAddConnectionCount(OutConnectionsCount, 1);
                if (!NT_SUCCESS(Status))
                    return Status;

                DescriptorCount++;
            }
            else if (InputTerminalDescriptor->bDescriptorSubtype == USB_AUDIO_OUTPUT_TERMINAL)
            {
                if (DescriptorCount >= NodeContextCount)
                    return STATUS_INVALID_DEVICE_REQUEST;

                OutputTerminalDescriptor = (PUSB_AUDIO_CONTROL_OUTPUT_TERMINAL_DESCRIPTOR)CommonDescriptor;
                if (UsbAudioResolveLastNodeForId(NodeContext,
                                                 NodeContextCount,
                                                 OutputTerminalDescriptor->bSourceID,
                                                 &SourceNode,
                                                 0))
                {
                    Status = UsbAudioCountSourceToNodeConnection(ConfigurationDescriptor,
                                                                OutConnectionsCount,
                                                                OutputTerminalDescriptor->bSourceID,
                                                                StreamingTerminalDescriptorCount);
                    if (!NT_SUCCESS(Status))
                        return Status;
                }

                Status = UsbAudioAddConnectionCount(OutConnectionsCount, 1);
                if (!NT_SUCCESS(Status))
                    return Status;

                DescriptorCount++;
            }
            else if (InputTerminalDescriptor->bDescriptorSubtype == USB_AUDIO_FEATURE_UNIT)
            {
                if (DescriptorCount >= NodeContextCount)
                    return STATUS_INVALID_DEVICE_REQUEST;

                FeatureUnitDescriptor = (PUSB_AUDIO_CONTROL_FEATURE_UNIT_DESCRIPTOR)InputTerminalDescriptor;
                if (NodeContext[DescriptorCount].NodeCount != 0 &&
                    UsbAudioResolveLastNodeForId(NodeContext,
                                                 NodeContextCount,
                                                 FeatureUnitDescriptor->bSourceID,
                                                 &SourceNode,
                                                 0))
                {
                    Status = UsbAudioCountSourceToNodeConnection(ConfigurationDescriptor,
                                                                OutConnectionsCount,
                                                                FeatureUnitDescriptor->bSourceID,
                                                                StreamingTerminalDescriptorCount);
                    if (!NT_SUCCESS(Status))
                        return Status;
                }

                if (NodeContext[DescriptorCount].NodeCount > 1)
                {
                    Status = UsbAudioAddConnectionCount(OutConnectionsCount,
                                                        NodeContext[DescriptorCount].NodeCount - 1);
                    if (!NT_SUCCESS(Status))
                        return Status;
                }

                DescriptorCount++;
            }
            else if (InputTerminalDescriptor->bDescriptorSubtype == USB_AUDIO_MIXER_UNIT)
            {
                if (DescriptorCount >= NodeContextCount)
                    return STATUS_INVALID_DEVICE_REQUEST;

                MixerUnitDescriptor = (PUSB_AUDIO_CONTROL_MIXER_UNIT_DESCRIPTOR)InputTerminalDescriptor;
                for (Index = 0; Index < MixerUnitDescriptor->bNrInPins; Index++)
                {
                    Value = MixerUnitDescriptor->baSourceID[Index];
                    if (UsbAudioResolveLastNodeForId(NodeContext,
                                                     NodeContextCount,
                                                     Value,
                                                     &SourceNode,
                                                     0))
                    {
                        Status = UsbAudioCountSourceToNodeConnection(ConfigurationDescriptor,
                                                                    OutConnectionsCount,
                                                                    Value,
                                                                    StreamingTerminalDescriptorCount);
                        if (!NT_SUCCESS(Status))
                            return Status;
                    }

                    Status = UsbAudioAddConnectionCount(OutConnectionsCount, 1);
                    if (!NT_SUCCESS(Status))
                        return Status;
                }

                DescriptorCount++;
            }
            else if (InputTerminalDescriptor->bDescriptorSubtype == USB_AUDIO_SELECTOR_UNIT)
            {
                if (DescriptorCount >= NodeContextCount)
                    return STATUS_INVALID_DEVICE_REQUEST;

                SelectorUnitDescriptor = (PUSB_AUDIO_CONTROL_SELECTOR_UNIT_DESCRIPTOR)InputTerminalDescriptor;
                for (Index = 0; Index < SelectorUnitDescriptor->bNrInPins; Index++)
                {
                    Value = SelectorUnitDescriptor->baSourceID[Index];
                    if (UsbAudioResolveLastNodeForId(NodeContext,
                                                     NodeContextCount,
                                                     Value,
                                                     &SourceNode,
                                                     0))
                    {
                        Status = UsbAudioCountSourceToNodeConnection(ConfigurationDescriptor,
                                                                    OutConnectionsCount,
                                                                    Value,
                                                                    StreamingTerminalDescriptorCount);
                        if (!NT_SUCCESS(Status))
                            return Status;
                    }
                }

                DescriptorCount++;
            }
            else if (DeviceExtension->AudioVersion == USB_AUDIO_VERSION_1 &&
                     (InputTerminalDescriptor->bDescriptorSubtype == USB_AUDIO1_PROCESSING_UNIT ||
                      InputTerminalDescriptor->bDescriptorSubtype == USB_AUDIO1_EXTENSION_UNIT))
            {
                if (DescriptorCount >= NodeContextCount)
                    return STATUS_INVALID_DEVICE_REQUEST;
                DescriptorCount++;
            }

            CommonDescriptor = UsbAudioNextControlDescriptor(ConfigurationDescriptor, InterfaceHeaderDescriptor, CommonDescriptor);
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS
BuildUSBAudioFilterTopology(
    PKSDEVICE Device,
    PKSFILTER_DESCRIPTOR FilterDescriptor)
{
    PDEVICE_EXTENSION DeviceExtension;
    PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor;
    ULONG NodeCount, Index, DescriptorCount, StreamingTerminalIndex, NonStreamingTerminalDescriptorCount, TotalTerminalDescriptorCount, StreamingTerminalPinOffset, StreamingTerminalDescriptorCount, RenderBridgePinCount, ControlDescriptorCount;
    ULONG ConnectionCapacity;
    UCHAR Value;
    PUSB_INTERFACE_DESCRIPTOR Descriptor;
    PUSB_AUDIO_CONTROL_INTERFACE_HEADER_DESCRIPTOR InterfaceHeaderDescriptor;
    PUSB_COMMON_DESCRIPTOR CommonDescriptor;
    PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR InputTerminalDescriptor;
    PUSB_AUDIO_CONTROL_FEATURE_UNIT_DESCRIPTOR FeatureUnitDescriptor;
    PUSB_AUDIO_CONTROL_MIXER_UNIT_DESCRIPTOR MixerUnitDescriptor;
    PUSB_AUDIO_CONTROL_OUTPUT_TERMINAL_DESCRIPTOR OutputTerminalDescriptor;
    PUSB_AUDIO_CONTROL_SELECTOR_UNIT_DESCRIPTOR SelectorUnitDescriptor;
    PKSNODE_DESCRIPTOR NodeDescriptors;
    PNODE_CONTEXT NodeContext;
    PKSTOPOLOGY_CONNECTION Connections;
    PKSAUTOMATION_TABLE AutomationTable;
    ULONG SourceNode;
    NTSTATUS Status;

    /* get device extension */
    DeviceExtension = Device->Context;
    ConfigurationDescriptor = DeviceExtension->ConfigurationDescriptor;

    /* count topology nodes */
    NodeCount = CountTopologyComponents(DeviceExtension, &ControlDescriptorCount);

    /* init node descriptors*/
    FilterDescriptor->NodeDescriptors = NodeDescriptors = AllocFunction(NodeCount * sizeof(KSNODE_DESCRIPTOR));
    if (FilterDescriptor->NodeDescriptors == NULL)
    {
        /* no memory */
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    FilterDescriptor->NodeDescriptorSize = sizeof(KSNODE_DESCRIPTOR);

    DeviceExtension->NodeContext = NodeContext = AllocFunction(sizeof(NODE_CONTEXT) * ControlDescriptorCount);
    if (!NodeContext)
    {
        /* no memory */
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    DeviceExtension->NodeContextCount = ControlDescriptorCount;
    DescriptorCount = 0;

    /* first enumerate all topology nodes */
    for (Descriptor = USBD_ParseConfigurationDescriptorEx(DeviceExtension->ConfigurationDescriptor, DeviceExtension->ConfigurationDescriptor, -1, -1, USB_DEVICE_CLASS_AUDIO, -1, -1);
    Descriptor != NULL;
        Descriptor = USBD_ParseConfigurationDescriptorEx(DeviceExtension->ConfigurationDescriptor, (PVOID)((ULONG_PTR)Descriptor + Descriptor->bLength), -1, -1, USB_DEVICE_CLASS_AUDIO, -1, -1))
    {
        if (Descriptor->bInterfaceSubClass == 0x01) /* AUDIO_CONTROL */
        {
            InterfaceHeaderDescriptor = (PUSB_AUDIO_CONTROL_INTERFACE_HEADER_DESCRIPTOR)USBD_ParseDescriptors(DeviceExtension->ConfigurationDescriptor, DeviceExtension->ConfigurationDescriptor->wTotalLength, Descriptor, USB_AUDIO_CONTROL_TERMINAL_DESCRIPTOR_TYPE);
            if (InterfaceHeaderDescriptor != NULL)
            {
                CommonDescriptor = UsbAudioFindControlDescriptor(ConfigurationDescriptor, InterfaceHeaderDescriptor);
                while (CommonDescriptor)
                {
                    InputTerminalDescriptor = (PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR)CommonDescriptor;
                    if (InputTerminalDescriptor->bDescriptorSubtype == USB_AUDIO_INPUT_TERMINAL ||
                        InputTerminalDescriptor->bDescriptorSubtype == USB_AUDIO_OUTPUT_TERMINAL)
                    {
                        UsbAudioAddTerminalNode(FilterDescriptor,
                                                NodeDescriptors,
                                                NodeContext,
                                                &DescriptorCount,
                                                CommonDescriptor);
                    }
                    else if (InputTerminalDescriptor->bDescriptorSubtype == 0x06 /* FEATURE_UNIT*/)
                    {
                        FeatureUnitDescriptor = (PUSB_AUDIO_CONTROL_FEATURE_UNIT_DESCRIPTOR)CommonDescriptor;

                        Value = UsbAudioGetFeatureUnitControls(CommonDescriptor,
                                                               DeviceExtension->AudioVersion);


                        if (Value & 0x01) /* MUTE*/
                        {
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].Type = &KSNODETYPE_MUTE;
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].Name = &KSNODETYPE_MUTE;
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].AutomationTable = AutomationTable = AllocFunction(sizeof(KSAUTOMATION_TABLE));
                            if (AutomationTable)
                            {
                                AutomationTable->PropertySets = FilterAudioMutePropertySetArray;
                                AutomationTable->PropertySetsCount = 1;
                                AutomationTable->PropertyItemSize = sizeof(KSPROPERTY_ITEM);
                            }

                            /* insert into node context*/
                            NodeContext[DescriptorCount].Nodes[NodeContext[DescriptorCount].NodeCount] = FilterDescriptor->NodeDescriptorsCount;
                            NodeContext[DescriptorCount].NodeCount++;

                            FilterDescriptor->NodeDescriptorsCount++;
                        }
                        if (Value & 0x02) /* VOLUME */
                        {
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].Type = &KSNODETYPE_VOLUME;
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].Name = &KSNODETYPE_VOLUME;
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].AutomationTable = AutomationTable = AllocFunction(sizeof(KSAUTOMATION_TABLE));
                            if (AutomationTable)
                            {
                                AutomationTable->PropertySets = FilterAudioVolumePropertySetArray;
                                AutomationTable->PropertySetsCount = 1;
                                AutomationTable->PropertyItemSize = sizeof(KSPROPERTY_ITEM);
                            }

                            /* insert into node context*/
                            NodeContext[DescriptorCount].Nodes[NodeContext[DescriptorCount].NodeCount] = FilterDescriptor->NodeDescriptorsCount;
                            NodeContext[DescriptorCount].NodeCount++;

                            FilterDescriptor->NodeDescriptorsCount++;
                        }

                        if (Value & 0x04) /* BASS */
                        {
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].Type = &KSNODETYPE_TONE;
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].Name = &KSNODETYPE_TONE;
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].AutomationTable = AllocFunction(sizeof(KSAUTOMATION_TABLE));

                            /* insert into node context*/
                            NodeContext[DescriptorCount].Nodes[NodeContext[DescriptorCount].NodeCount] = FilterDescriptor->NodeDescriptorsCount;
                            NodeContext[DescriptorCount].NodeCount++;

                            FilterDescriptor->NodeDescriptorsCount++;
                        }

                        if (Value & 0x08) /* MID */
                        {
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].Type = &KSNODETYPE_TONE;
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].Name = &KSNODETYPE_TONE;
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].AutomationTable = AllocFunction(sizeof(KSAUTOMATION_TABLE));

                            /* insert into node context*/
                            NodeContext[DescriptorCount].Nodes[NodeContext[DescriptorCount].NodeCount] = FilterDescriptor->NodeDescriptorsCount;
                            NodeContext[DescriptorCount].NodeCount++;

                            FilterDescriptor->NodeDescriptorsCount++;
                        }

                        if (Value & 0x10) /* TREBLE */
                        {
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].Type = &KSNODETYPE_TONE;
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].Name = &KSNODETYPE_TONE;
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].AutomationTable = AllocFunction(sizeof(KSAUTOMATION_TABLE));

                            /* insert into node context*/
                            NodeContext[DescriptorCount].Nodes[NodeContext[DescriptorCount].NodeCount] = FilterDescriptor->NodeDescriptorsCount;
                            NodeContext[DescriptorCount].NodeCount++;


                            FilterDescriptor->NodeDescriptorsCount++;
                        }

                        if (Value & 0x20) /* GRAPHIC EQUALIZER */
                        {
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].Type = &KSNODETYPE_TONE;
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].Name = &KSNODETYPE_TONE;
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].AutomationTable = AllocFunction(sizeof(KSAUTOMATION_TABLE));

                            /* insert into node context*/
                            NodeContext[DescriptorCount].Nodes[NodeContext[DescriptorCount].NodeCount] = FilterDescriptor->NodeDescriptorsCount;
                            NodeContext[DescriptorCount].NodeCount++;

                            FilterDescriptor->NodeDescriptorsCount++;
                        }

                        if (Value & 0x40) /* AUTOMATIC GAIN */
                        {
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].Type = &KSNODETYPE_AGC;
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].Name = &KSNODETYPE_AGC;
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].AutomationTable = AllocFunction(sizeof(KSAUTOMATION_TABLE));

                            /* insert into node context*/
                            NodeContext[DescriptorCount].Nodes[NodeContext[DescriptorCount].NodeCount] = FilterDescriptor->NodeDescriptorsCount;
                            NodeContext[DescriptorCount].NodeCount++;


                            FilterDescriptor->NodeDescriptorsCount++;
                        }

                        if (Value & 0x80) /* DELAY */
                        {
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].Type = &KSNODETYPE_TONE;
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].Name = &KSNODETYPE_TONE;
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].AutomationTable = AllocFunction(sizeof(KSAUTOMATION_TABLE));

                            /* insert into node context*/
                            NodeContext[DescriptorCount].Nodes[NodeContext[DescriptorCount].NodeCount] = FilterDescriptor->NodeDescriptorsCount;
                            NodeContext[DescriptorCount].NodeCount++;

                            FilterDescriptor->NodeDescriptorsCount++;
                        }
                        NodeContext[DescriptorCount].Descriptor = CommonDescriptor;
                        {
                            Status = UsbAudioInitializeFeatureUnitCache(Device,
                                                                        DeviceExtension,
                                                                        &NodeContext[DescriptorCount],
                                                                        FeatureUnitDescriptor);
                            if (!NT_SUCCESS(Status))
                                return Status;
                        }
                        DescriptorCount++;

                    }
                    else if (InputTerminalDescriptor->bDescriptorSubtype == 0x04 /* MIXER_UNIT */)
                    {
                        MixerUnitDescriptor = (PUSB_AUDIO_CONTROL_MIXER_UNIT_DESCRIPTOR)CommonDescriptor;
                        if (MixerUnitDescriptor->bNrInPins + 1 > USBAUDIO_MAX_NODES_PER_CONTEXT)
                        {
                            DPRINT1("BuildUSBAudioFilterTopology: mixer unit %u has too many pins %u\n",
                                    MixerUnitDescriptor->bUnitID,
                                    MixerUnitDescriptor->bNrInPins);
                            return STATUS_INVALID_DEVICE_REQUEST;
                        }

                        for (Index = 0; Index < MixerUnitDescriptor->bNrInPins; Index++)
                        {
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].Type = &KSNODETYPE_SUPERMIX;
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].Name = &KSNODETYPE_SUPERMIX;
                            NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].AutomationTable = AllocFunction(sizeof(KSAUTOMATION_TABLE));

                            /* insert into node context*/
                            NodeContext[DescriptorCount].Nodes[NodeContext[DescriptorCount].NodeCount] = FilterDescriptor->NodeDescriptorsCount;
                            NodeContext[DescriptorCount].NodeCount++;

                            FilterDescriptor->NodeDescriptorsCount++;
                        }

                        NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].Type = &KSNODETYPE_SUM;
                        NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].Name = &KSNODETYPE_SUM;
                        NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].AutomationTable = AllocFunction(sizeof(KSAUTOMATION_TABLE));

                        /* insert into node context*/
                        NodeContext[DescriptorCount].Nodes[NodeContext[DescriptorCount].NodeCount] = FilterDescriptor->NodeDescriptorsCount;
                        NodeContext[DescriptorCount].NodeCount++;
                        NodeContext[DescriptorCount].Descriptor = CommonDescriptor;
                        DescriptorCount++;

                        FilterDescriptor->NodeDescriptorsCount++;
                    }
                    else if (InputTerminalDescriptor->bDescriptorSubtype == 0x05 /* SELECTOR UNIT */)
                    {
                        NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].Type = &KSNODETYPE_MUX;
                        NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].Name = &KSNODETYPE_MUX;
                        NodeDescriptors[FilterDescriptor->NodeDescriptorsCount].AutomationTable = AllocFunction(sizeof(KSAUTOMATION_TABLE));

                        /* insert into node context*/
                        NodeContext[DescriptorCount].Descriptor = CommonDescriptor;
                        NodeContext[DescriptorCount].NodeCount = 1;
                        NodeContext[DescriptorCount].Nodes[0] = FilterDescriptor->NodeDescriptorsCount;
                        DescriptorCount++;
                        FilterDescriptor->NodeDescriptorsCount++;
                    }
                    else if (DeviceExtension->AudioVersion == USB_AUDIO_VERSION_1 &&
                             (InputTerminalDescriptor->bDescriptorSubtype == USB_AUDIO1_PROCESSING_UNIT ||
                              InputTerminalDescriptor->bDescriptorSubtype == USB_AUDIO1_EXTENSION_UNIT))
                    {
                        NodeContext[DescriptorCount].Descriptor = CommonDescriptor;
                        DescriptorCount++;
                    }
                    else
                    {
                        DPRINT1("BuildUSBAudioFilterTopology: unknown descriptor subtype %x\n",
                                InputTerminalDescriptor->bDescriptorSubtype);
                    }
                    CommonDescriptor = UsbAudioNextControlDescriptor(ConfigurationDescriptor, InterfaceHeaderDescriptor, CommonDescriptor);
                }
            }
        }
    }

    CountTerminalUnits(DeviceExtension->ConfigurationDescriptor, &NonStreamingTerminalDescriptorCount, &TotalTerminalDescriptorCount);
    if (NonStreamingTerminalDescriptorCount > TotalTerminalDescriptorCount)
        return STATUS_INVALID_DEVICE_REQUEST;

    StreamingTerminalDescriptorCount = TotalTerminalDescriptorCount - NonStreamingTerminalDescriptorCount;
    RenderBridgePinCount = UsbAudioCountRenderBridgePins(DeviceExtension->ConfigurationDescriptor);
    StreamingTerminalPinOffset = StreamingTerminalDescriptorCount + RenderBridgePinCount;

    Status = UsbAudioCountTopologyConnections(DeviceExtension,
                                              NodeContext,
                                              ControlDescriptorCount,
                                              StreamingTerminalDescriptorCount,
                                              &ConnectionCapacity);
    if (!NT_SUCCESS(Status))
        return Status;

    if (ConnectionCapacity == 0)
        return STATUS_INVALID_DEVICE_REQUEST;

    if (ConnectionCapacity > MAXULONG / sizeof(KSTOPOLOGY_CONNECTION))
        return STATUS_INTEGER_OVERFLOW;

    /* Allocate exactly the topology connections this descriptor graph can emit. */
    FilterDescriptor->Connections = Connections =
        AllocFunction(sizeof(KSTOPOLOGY_CONNECTION) * ConnectionCapacity);
    if (!FilterDescriptor->Connections)
    {
        /* no memory */
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    FilterDescriptor->ConnectionsCount = 0;

    /* now build connections array */
    DescriptorCount = 0;
    StreamingTerminalIndex = 0;
    NodeCount = 0;

    for (Descriptor = USBD_ParseConfigurationDescriptorEx(DeviceExtension->ConfigurationDescriptor, DeviceExtension->ConfigurationDescriptor, -1, -1, USB_DEVICE_CLASS_AUDIO, -1, -1);
    Descriptor != NULL;
        Descriptor = USBD_ParseConfigurationDescriptorEx(DeviceExtension->ConfigurationDescriptor, (PVOID)((ULONG_PTR)Descriptor + Descriptor->bLength), -1, -1, USB_DEVICE_CLASS_AUDIO, -1, -1))
    {
        if (Descriptor->bInterfaceSubClass == 0x01) /* AUDIO_CONTROL */
        {
            InterfaceHeaderDescriptor = (PUSB_AUDIO_CONTROL_INTERFACE_HEADER_DESCRIPTOR)USBD_ParseDescriptors(DeviceExtension->ConfigurationDescriptor, DeviceExtension->ConfigurationDescriptor->wTotalLength, Descriptor, USB_AUDIO_CONTROL_TERMINAL_DESCRIPTOR_TYPE);
            if (InterfaceHeaderDescriptor != NULL)
            {
                CommonDescriptor = UsbAudioFindControlDescriptor(ConfigurationDescriptor, InterfaceHeaderDescriptor);
                while (CommonDescriptor)
                {
                    InputTerminalDescriptor = (PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR)CommonDescriptor;
                    if (InputTerminalDescriptor->bDescriptorSubtype == USB_AUDIO_INPUT_TERMINAL)
                    {
                        if (InputTerminalDescriptor->wTerminalType == USB_AUDIO_STREAMING_TERMINAL_TYPE)
                        {
                             Status = UsbAudioAppendTopologyConnection(Connections,
                                                                       ConnectionCapacity,
                                                                       &FilterDescriptor->ConnectionsCount,
                                                                       KSFILTER_NODE,
                                                                       StreamingTerminalIndex,
                                                                       NodeContext[DescriptorCount].Nodes[0],
                                                                       1);
                             if (!NT_SUCCESS(Status))
                                 return Status;

                             StreamingTerminalIndex++;

                        }
                        else
                        {
                            Status = UsbAudioAppendTopologyConnection(Connections,
                                                                      ConnectionCapacity,
                                                                      &FilterDescriptor->ConnectionsCount,
                                                                      KSFILTER_NODE,
                                                                      StreamingTerminalPinOffset,
                                                                      NodeContext[DescriptorCount].Nodes[0],
                                                                      1);
                            if (!NT_SUCCESS(Status))
                                return Status;

                            StreamingTerminalPinOffset++;
                        }
                        DescriptorCount++;
                    }
                    else if (InputTerminalDescriptor->bDescriptorSubtype == USB_AUDIO_OUTPUT_TERMINAL)
                    {
                        OutputTerminalDescriptor = (PUSB_AUDIO_CONTROL_OUTPUT_TERMINAL_DESCRIPTOR)CommonDescriptor;
                        if (UsbAudioResolveLastNodeForId(NodeContext,
                                                         ControlDescriptorCount,
                                                         OutputTerminalDescriptor->bSourceID,
                                                         &SourceNode,
                                                         0))
                        {
                            Status = UsbAudioAddSourceToNodeConnection(DeviceExtension->ConfigurationDescriptor,
                                                                       Connections,
                                                                       ConnectionCapacity,
                                                                       &FilterDescriptor->ConnectionsCount,
                                                                       OutputTerminalDescriptor->bSourceID,
                                                                       SourceNode,
                                                                       NodeContext[DescriptorCount].Nodes[0],
                                                                       1,
                                                                       StreamingTerminalDescriptorCount);
                            if (!NT_SUCCESS(Status))
                                return Status;
                        }

                        if (InputTerminalDescriptor->wTerminalType == USB_AUDIO_STREAMING_TERMINAL_TYPE)
                        {
                            Status = UsbAudioAppendTopologyConnection(Connections,
                                                                      ConnectionCapacity,
                                                                      &FilterDescriptor->ConnectionsCount,
                                                                      NodeContext[DescriptorCount].Nodes[0],
                                                                      0,
                                                                      KSFILTER_NODE,
                                                                      StreamingTerminalIndex);
                            if (!NT_SUCCESS(Status))
                                return Status;

                            StreamingTerminalIndex++;
                        }
                        else
                        {
                            Status = UsbAudioAppendTopologyConnection(Connections,
                                                                      ConnectionCapacity,
                                                                      &FilterDescriptor->ConnectionsCount,
                                                                      NodeContext[DescriptorCount].Nodes[0],
                                                                      0,
                                                                      KSFILTER_NODE,
                                                                      StreamingTerminalPinOffset);
                            if (!NT_SUCCESS(Status))
                                return Status;

                            StreamingTerminalPinOffset++;
                        }
                        DescriptorCount++;
                    }
                    else if (InputTerminalDescriptor->bDescriptorSubtype == USB_AUDIO_FEATURE_UNIT)
                    {
                        FeatureUnitDescriptor = (PUSB_AUDIO_CONTROL_FEATURE_UNIT_DESCRIPTOR)InputTerminalDescriptor;
                        if (NodeContext[DescriptorCount].NodeCount != 0 &&
                            UsbAudioResolveLastNodeForId(NodeContext,
                                                         ControlDescriptorCount,
                                                         FeatureUnitDescriptor->bSourceID,
                                                         &SourceNode,
                                                         0))
                        {
                            Status = UsbAudioAddSourceToNodeConnection(DeviceExtension->ConfigurationDescriptor,
                                                                       Connections,
                                                                       ConnectionCapacity,
                                                                       &FilterDescriptor->ConnectionsCount,
                                                                       FeatureUnitDescriptor->bSourceID,
                                                                       SourceNode,
                                                                       NodeContext[DescriptorCount].Nodes[0],
                                                                       1,
                                                                       StreamingTerminalDescriptorCount);
                            if (!NT_SUCCESS(Status))
                                return Status;
                        }
                        for (Index = 1; Index < NodeContext[DescriptorCount].NodeCount; Index++)
                        {
                            Status = UsbAudioAppendTopologyConnection(Connections,
                                                                      ConnectionCapacity,
                                                                      &FilterDescriptor->ConnectionsCount,
                                                                      NodeContext[DescriptorCount].Nodes[Index - 1],
                                                                      0,
                                                                      NodeContext[DescriptorCount].Nodes[Index],
                                                                      1);
                            if (!NT_SUCCESS(Status))
                                return Status;
                        }

                        DescriptorCount++;
                    }
                    else if (InputTerminalDescriptor->bDescriptorSubtype == USB_AUDIO_MIXER_UNIT)
                    {
                        MixerUnitDescriptor = (PUSB_AUDIO_CONTROL_MIXER_UNIT_DESCRIPTOR)InputTerminalDescriptor;
                        for (Index = 0; Index < MixerUnitDescriptor->bNrInPins; Index++)
                        {
                            Value = MixerUnitDescriptor->baSourceID[Index];
                            if (UsbAudioResolveLastNodeForId(NodeContext,
                                                             ControlDescriptorCount,
                                                             Value,
                                                             &SourceNode,
                                                             0))
                            {
                                Status = UsbAudioAddSourceToNodeConnection(DeviceExtension->ConfigurationDescriptor,
                                                                           Connections,
                                                                           ConnectionCapacity,
                                                                           &FilterDescriptor->ConnectionsCount,
                                                                           Value,
                                                                           SourceNode,
                                                                           NodeContext[DescriptorCount].Nodes[Index],
                                                                           1,
                                                                           StreamingTerminalDescriptorCount);
                                if (!NT_SUCCESS(Status))
                                    return Status;
                            }

                            Status = UsbAudioAppendTopologyConnection(Connections,
                                                                      ConnectionCapacity,
                                                                      &FilterDescriptor->ConnectionsCount,
                                                                      NodeContext[DescriptorCount].Nodes[Index],
                                                                      0,
                                                                      NodeContext[DescriptorCount].Nodes[NodeContext[DescriptorCount].NodeCount - 1],
                                                                      1 + Index);
                            if (!NT_SUCCESS(Status))
                                return Status;
                        }
                        DescriptorCount++;
                    }
                    else if (InputTerminalDescriptor->bDescriptorSubtype == USB_AUDIO_SELECTOR_UNIT)
                    {
                        SelectorUnitDescriptor = (PUSB_AUDIO_CONTROL_SELECTOR_UNIT_DESCRIPTOR)InputTerminalDescriptor;
                        for (Index = 0; Index < SelectorUnitDescriptor->bNrInPins; Index++)
                        {
                            Value = SelectorUnitDescriptor->baSourceID[Index];
                            if (UsbAudioResolveLastNodeForId(NodeContext,
                                                             ControlDescriptorCount,
                                                             Value,
                                                             &SourceNode,
                                                             0))
                            {
                                Status = UsbAudioAddSourceToNodeConnection(DeviceExtension->ConfigurationDescriptor,
                                                                           Connections,
                                                                           ConnectionCapacity,
                                                                           &FilterDescriptor->ConnectionsCount,
                                                                           Value,
                                                                           SourceNode,
                                                                           NodeContext[DescriptorCount].Nodes[0],
                                                                           1,
                                                                           StreamingTerminalDescriptorCount);
                                if (!NT_SUCCESS(Status))
                                    return Status;
                            }
                        }
                        DescriptorCount++;
                    }
                    else if (DeviceExtension->AudioVersion == USB_AUDIO_VERSION_1 &&
                             (InputTerminalDescriptor->bDescriptorSubtype == USB_AUDIO1_PROCESSING_UNIT ||
                              InputTerminalDescriptor->bDescriptorSubtype == USB_AUDIO1_EXTENSION_UNIT))
                    {
                        DescriptorCount++;
                    }
                    else
                    {
                        DPRINT1("BuildUSBAudioFilterTopology: unknown descriptor subtype %x\n",
                                InputTerminalDescriptor->bDescriptorSubtype);
                    }
                    CommonDescriptor = UsbAudioNextControlDescriptor(ConfigurationDescriptor, InterfaceHeaderDescriptor, CommonDescriptor);
                }
            }
        }
    }



    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
USBAudioFilterCreate(
    PKSFILTER Filter,
    PIRP Irp)
{
    PKSFILTERFACTORY FilterFactory;
    PKSDEVICE Device;
    PFILTER_CONTEXT FilterContext;

    FilterFactory = KsGetParent(Filter);
    if (FilterFactory == NULL)
    {
        /* invalid parameter */
        return STATUS_INVALID_PARAMETER;
    }

    Device = KsGetParent(FilterFactory);
    if (Device == NULL)
    {
        /* invalid parameter */
        return STATUS_INVALID_PARAMETER;
    }

    /* alloc filter context */
    FilterContext = AllocFunction(sizeof(FILTER_CONTEXT));
    if (FilterContext == NULL)
    {
        /* no memory */
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* init context */
    FilterContext->DeviceExtension = Device->Context;
    FilterContext->LowerDevice = Device->NextDeviceObject;
    Filter->Context = FilterContext;

    DPRINT("USBAudioFilterCreate FilterContext %p LowerDevice %p DeviceExtension %p\n", FilterContext, FilterContext->LowerDevice, FilterContext->DeviceExtension);
    if (!NT_SUCCESS(KsAddItemToObjectBag(Filter->Bag, FilterContext, ExFreePool)))
    {
        Filter->Context = NULL;
        FreeFunction(FilterContext);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    return STATUS_SUCCESS;
}


VOID
NTAPI
CountTerminalUnits(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    OUT PULONG NonStreamingTerminalDescriptorCount,
    OUT PULONG TotalTerminalDescriptorCount)
{
    PUSB_INTERFACE_DESCRIPTOR Descriptor;
    PUSB_AUDIO_CONTROL_INTERFACE_HEADER_DESCRIPTOR InterfaceHeaderDescriptor;
    PUSB_COMMON_DESCRIPTOR CommonDescriptor;
    PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR InputTerminalDescriptor;
    ULONG NonStreamingTerminalCount = 0;
    ULONG TotalTerminalCount = 0;

    for(Descriptor = USBD_ParseConfigurationDescriptorEx(ConfigurationDescriptor, ConfigurationDescriptor, -1, -1, USB_DEVICE_CLASS_AUDIO, -1, -1);
        Descriptor != NULL;
        Descriptor = USBD_ParseConfigurationDescriptorEx(ConfigurationDescriptor, (PVOID)((ULONG_PTR)Descriptor + Descriptor->bLength), -1, -1, USB_DEVICE_CLASS_AUDIO, -1, -1))
    {
        if (Descriptor->bInterfaceSubClass == 0x01) /* AUDIO_CONTROL */
        {
            InterfaceHeaderDescriptor = (PUSB_AUDIO_CONTROL_INTERFACE_HEADER_DESCRIPTOR)USBD_ParseDescriptors(ConfigurationDescriptor, ConfigurationDescriptor->wTotalLength, Descriptor, USB_AUDIO_CONTROL_TERMINAL_DESCRIPTOR_TYPE);
            if (InterfaceHeaderDescriptor != NULL)
            {
                CommonDescriptor = UsbAudioFindControlDescriptor(ConfigurationDescriptor, InterfaceHeaderDescriptor);
                while (CommonDescriptor)
                {
                    InputTerminalDescriptor = (PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR)CommonDescriptor;
                    if (InputTerminalDescriptor->bDescriptorSubtype == 0x02 /* INPUT TERMINAL*/ || InputTerminalDescriptor->bDescriptorSubtype == 0x03 /* OUTPUT_TERMINAL*/)
                    {
                        if (InputTerminalDescriptor->wTerminalType != USB_AUDIO_STREAMING_TERMINAL_TYPE)
                        {
                            NonStreamingTerminalCount++;
                        }
                        TotalTerminalCount++;
                    }
                    CommonDescriptor = UsbAudioNextControlDescriptor(ConfigurationDescriptor, InterfaceHeaderDescriptor, CommonDescriptor);
                }
            }
        }
        else if (Descriptor->bInterfaceSubClass == 0x03) /* MIDI_STREAMING */
        {
            /* MIDI streaming interfaces are claimed for future use
             * but do not contribute to audio terminal topology */
        }
    }
    *NonStreamingTerminalDescriptorCount = NonStreamingTerminalCount;
    *TotalTerminalDescriptorCount = TotalTerminalCount;
}

LPGUID
UsbAudioGetPinCategoryFromTerminalDescriptor(
    IN PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR TerminalDescriptor)
{
    USHORT TerminalGroup;

    if (!TerminalDescriptor)
        return &GUID_KSCATEGORY_AUDIO;

    if (TerminalDescriptor->wTerminalType == USB_AUDIO_MICROPHONE_TERMINAL_TYPE)
        return &NodeTypeMicrophone;
    else if (TerminalDescriptor->wTerminalType == USB_AUDIO_DESKTOP_MICROPHONE_TERMINAL_TYPE)
        return &NodeTypeDesktopMicrophone;
    else if (TerminalDescriptor->wTerminalType == USB_AUDIO_PERSONAL_MICROPHONE_TERMINAL_TYPE)
        return &NodeTypePersonalMicrophone;
    else if (TerminalDescriptor->wTerminalType == USB_AUDIO_OMMNI_MICROPHONE_TERMINAL_TYPE)
        return &NodeTypeOmmniMicrophone;
    else if (TerminalDescriptor->wTerminalType == USB_AUDIO_ARRAY_MICROPHONE_TERMINAL_TYPE)
        return &NodeTypeArrayMicrophone;
    else if (TerminalDescriptor->wTerminalType == USB_AUDIO_ARRAY_PROCESSING_MICROPHONE_TERMINAL_TYPE)
        return &NodeTypeProcessingArrayMicrophone;

    /* playback types */
    if (TerminalDescriptor->wTerminalType == USB_AUDIO_SPEAKER_TERMINAL_TYPE)
        return &NodeTypeSpeaker;
    else if (TerminalDescriptor->wTerminalType == USB_HEADPHONES_SPEAKER_TERMINAL_TYPE)
        return &NodeTypeHeadphonesSpeaker;
    else if (TerminalDescriptor->wTerminalType == USB_AUDIO_HMDA_TERMINAL_TYPE)
        return &NodeTypeHMDA;
    else if (TerminalDescriptor->wTerminalType == USB_AUDIO_DESKTOP_SPEAKER_TERMINAL_TYPE)
        return &NodeTypeDesktopSpeaker;
    else if (TerminalDescriptor->wTerminalType == USB_AUDIO_ROOM_SPEAKER_TERMINAL_TYPE)
        return &NodeTypeRoomSpeaker;
    else if (TerminalDescriptor->wTerminalType == USB_AUDIO_COMMUNICATION_SPEAKER_TERMINAL_TYPE)
        return &NodeTypeCommunicationSpeaker;
    else if (TerminalDescriptor->wTerminalType == USB_AUDIO_SUBWOOFER_TERMINAL_TYPE)
        return &NodeTypeSubwoofer;

    /* bidirectional terminal types */
    if (TerminalDescriptor->wTerminalType == USB_AUDIO_UNDEFINED_BIDIRECTIONAL_TERMINAL_TYPE)
        return &NodeTypeBidirectionalUndefined;
    else if (TerminalDescriptor->wTerminalType == USB_AUDIO_HANDSET_TERMINAL_TYPE)
        return &NodeTypeHandset;
    else if (TerminalDescriptor->wTerminalType == USB_AUDIO_HEADSET_TERMINAL_TYPE)
        return &NodeTypeHeadset;
    else if (TerminalDescriptor->wTerminalType == USB_AUDIO_SPEAKERPHONE_TERMINAL_TYPE)
        return &NodeTypeSpeakerphone;
    else if (TerminalDescriptor->wTerminalType == USB_AUDIO_ECHO_SUPPRESSING_SPEAKERPHONE_TERMINAL_TYPE)
        return &NodeTypeEchoSuppressingSpeakerphone;
    else if (TerminalDescriptor->wTerminalType == USB_AUDIO_ECHO_CANCELING_SPEAKERPHONE_TERMINAL_TYPE)
        return &NodeTypeEchoCancelingSpeakerphone;

    if (TerminalDescriptor->wTerminalType == USB_AUDIO_STREAMING_TERMINAL_TYPE)
    {
        if (TerminalDescriptor->bDescriptorSubtype == USB_AUDIO_OUTPUT_TERMINAL)
            return &NodeTypeCapture;
        else if (TerminalDescriptor->bDescriptorSubtype == USB_AUDIO_INPUT_TERMINAL)
            return &NodeTypePlayback;

    }

    TerminalGroup = TerminalDescriptor->wTerminalType & USB_AUDIO_TERMINAL_TYPE_GROUP_MASK;
    if (TerminalGroup == USB_AUDIO_BIDIRECTIONAL_TERMINAL_TYPE_GROUP)
        return &NodeTypeBidirectionalUndefined;
    else if (TerminalGroup == USB_AUDIO_TELEPHONY_TERMINAL_TYPE_GROUP)
        return &NodeTypePhoneLine;
    else if (TerminalGroup == USB_AUDIO_EXTERNAL_TERMINAL_TYPE_GROUP)
        return &NodeTypeLineConnector;
    else if (TerminalGroup == USB_AUDIO_EMBEDDED_FUNCTION_TERMINAL_TYPE_GROUP)
        return &NodeTypeAnalogConnector;

    if (TerminalDescriptor->bDescriptorSubtype == USB_AUDIO_OUTPUT_TERMINAL)
        return &NodeTypeSpeaker;
    return &NodeTypeMicrophone;
}

PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR
UsbAudioGetStreamingTerminalDescriptorByIndex(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    IN ULONG Index)
{
    PUSB_INTERFACE_DESCRIPTOR Descriptor;
    PUSB_AUDIO_CONTROL_INTERFACE_HEADER_DESCRIPTOR InterfaceHeaderDescriptor;
    PUSB_COMMON_DESCRIPTOR CommonDescriptor;
    PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR InputTerminalDescriptor;
    ULONG TerminalCount = 0;

    for (Descriptor = USBD_ParseConfigurationDescriptorEx(ConfigurationDescriptor, ConfigurationDescriptor, -1, -1, USB_DEVICE_CLASS_AUDIO, -1, -1);
    Descriptor != NULL;
        Descriptor = USBD_ParseConfigurationDescriptorEx(ConfigurationDescriptor, (PVOID)((ULONG_PTR)Descriptor + Descriptor->bLength), -1, -1, USB_DEVICE_CLASS_AUDIO, -1, -1))
    {
        if (Descriptor->bInterfaceSubClass == 0x01) /* AUDIO_CONTROL */
        {
            InterfaceHeaderDescriptor = (PUSB_AUDIO_CONTROL_INTERFACE_HEADER_DESCRIPTOR)USBD_ParseDescriptors(ConfigurationDescriptor, ConfigurationDescriptor->wTotalLength, Descriptor, USB_AUDIO_CONTROL_TERMINAL_DESCRIPTOR_TYPE);
            if (InterfaceHeaderDescriptor != NULL)
            {
                CommonDescriptor = UsbAudioFindControlDescriptor(ConfigurationDescriptor, InterfaceHeaderDescriptor);
                while (CommonDescriptor)
                {
                    InputTerminalDescriptor = (PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR)CommonDescriptor;
                    if (InputTerminalDescriptor->bDescriptorSubtype == 0x02 /* INPUT TERMINAL*/ || InputTerminalDescriptor->bDescriptorSubtype == 0x03 /* OUTPUT_TERMINAL*/)
                    {
                        if (InputTerminalDescriptor->wTerminalType == USB_AUDIO_STREAMING_TERMINAL_TYPE)
                        {
                            if (TerminalCount == Index)
                            {
                                return InputTerminalDescriptor;
                            }
                            TerminalCount++;
                        }
                    }
                    CommonDescriptor = UsbAudioNextControlDescriptor(ConfigurationDescriptor, InterfaceHeaderDescriptor, CommonDescriptor);
                }
            }
        }
    }
    return NULL;
}

PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR
UsbAudioGetNonStreamingTerminalDescriptorByIndex(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    IN ULONG Index)
{

    PUSB_INTERFACE_DESCRIPTOR Descriptor;
    PUSB_AUDIO_CONTROL_INTERFACE_HEADER_DESCRIPTOR InterfaceHeaderDescriptor;
    PUSB_COMMON_DESCRIPTOR CommonDescriptor;
    PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR InputTerminalDescriptor;
    ULONG TerminalCount = 0;

    for (Descriptor = USBD_ParseConfigurationDescriptorEx(ConfigurationDescriptor, ConfigurationDescriptor, -1, -1, USB_DEVICE_CLASS_AUDIO, -1, -1);
    Descriptor != NULL;
        Descriptor = USBD_ParseConfigurationDescriptorEx(ConfigurationDescriptor, (PVOID)((ULONG_PTR)Descriptor + Descriptor->bLength), -1, -1, USB_DEVICE_CLASS_AUDIO, -1, -1))
    {
        if (Descriptor->bInterfaceSubClass == 0x01) /* AUDIO_CONTROL */
        {
            InterfaceHeaderDescriptor = (PUSB_AUDIO_CONTROL_INTERFACE_HEADER_DESCRIPTOR)USBD_ParseDescriptors(ConfigurationDescriptor, ConfigurationDescriptor->wTotalLength, Descriptor, USB_AUDIO_CONTROL_TERMINAL_DESCRIPTOR_TYPE);
            if (InterfaceHeaderDescriptor != NULL)
            {
                CommonDescriptor = UsbAudioFindControlDescriptor(ConfigurationDescriptor, InterfaceHeaderDescriptor);
                while (CommonDescriptor)
                {
                    InputTerminalDescriptor = (PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR)CommonDescriptor;
                    if (InputTerminalDescriptor->bDescriptorSubtype == 0x02 /* INPUT TERMINAL*/ || InputTerminalDescriptor->bDescriptorSubtype == 0x03 /* OUTPUT_TERMINAL*/)
                    {
                        if (InputTerminalDescriptor->wTerminalType != USB_AUDIO_STREAMING_TERMINAL_TYPE)
                        {
                            if (TerminalCount == Index)
                            {
                                return InputTerminalDescriptor;
                            }
                            TerminalCount++;
                        }
                    }
                    CommonDescriptor = UsbAudioNextControlDescriptor(ConfigurationDescriptor, InterfaceHeaderDescriptor, CommonDescriptor);
                }
            }
        }
    }
    return NULL;
}

static
ULONG
UsbAudioReadSampleFrequency(
    IN const UCHAR *Frequency)
{
    return ((ULONG)Frequency[0]) |
           ((ULONG)Frequency[1] << 8) |
           ((ULONG)Frequency[2] << 16);
}

PUSB_COMMON_DESCRIPTOR
UsbAudioFindStreamingClassDescriptor(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    IN PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor,
    IN UCHAR Subtype)
{
    PUCHAR ConfigurationEnd;
    PUSB_COMMON_DESCRIPTOR CommonDescriptor;

    ConfigurationEnd = (PUCHAR)ConfigurationDescriptor +
                       ConfigurationDescriptor->wTotalLength;
    CommonDescriptor = (PUSB_COMMON_DESCRIPTOR)((PUCHAR)InterfaceDescriptor +
                                                InterfaceDescriptor->bLength);

    while ((PUCHAR)CommonDescriptor + 3 <= ConfigurationEnd &&
           CommonDescriptor->bLength >= 3 &&
           (PUCHAR)CommonDescriptor + CommonDescriptor->bLength <= ConfigurationEnd)
    {
        if (CommonDescriptor->bDescriptorType == USB_INTERFACE_DESCRIPTOR_TYPE)
            break;

        if (CommonDescriptor->bDescriptorType == USB_AUDIO_CONTROL_TERMINAL_DESCRIPTOR_TYPE &&
            ((PUCHAR)CommonDescriptor)[2] == Subtype)
        {
            return CommonDescriptor;
        }

        CommonDescriptor = (PUSB_COMMON_DESCRIPTOR)((PUCHAR)CommonDescriptor +
                                                    CommonDescriptor->bLength);
    }

    return NULL;
}

UCHAR
UsbAudioGetTerminalClockId(
    IN PDEVICE_EXTENSION DeviceExtension,
    IN UCHAR TerminalId)
{
    PUCHAR Cursor;
    PUCHAR End;

    if (!NT_SUCCESS(UsbAudioGetControlDescriptorBounds(DeviceExtension, &Cursor, &End)))
        return 0;

    while (Cursor + 4 <= End)
    {
        if (Cursor[0] < 4)
            break;

        if (Cursor[1] == USB_AUDIO_CONTROL_TERMINAL_DESCRIPTOR_TYPE &&
            Cursor[3] == TerminalId &&
            Cursor[2] != 0x01)
        {
            if (Cursor[2] == USB_AUDIO_INPUT_TERMINAL && Cursor[0] >= 8)
                return Cursor[7];
            if (Cursor[2] == USB_AUDIO_OUTPUT_TERMINAL && Cursor[0] >= 9)
                return Cursor[8];
            return 0;
        }

        Cursor += Cursor[0];
    }

    return 0;
}

static
ULONG
UsbAudioReadLittleEndian32(
    IN const UCHAR *Buffer)
{
    return ((ULONG)Buffer[0]) |
           ((ULONG)Buffer[1] << 8) |
           ((ULONG)Buffer[2] << 16) |
           ((ULONG)Buffer[3] << 24);
}

static
NTSTATUS
UsbAudioQueryClockRange(
    IN PDEVICE_EXTENSION DeviceExtension,
    IN UCHAR ClockId,
    OUT PULONG MinimumRate,
    OUT PULONG MaximumRate)
{
    PUSBAUDIO_CLOCK_RANGE Cache = NULL;
    UCHAR Header[2];
    PUCHAR Data;
    NTSTATUS Status;
    ULONG Count;
    ULONG Index;
    ULONG Minimum = MAXULONG;
    ULONG Maximum = 0;
    USHORT RequestIndex;

    *MinimumRate = 0;
    *MaximumRate = 0;

    for (Index = 0; Index < DeviceExtension->ClockRangeCount; Index++)
    {
        if (DeviceExtension->ClockRanges[Index].ClockId == ClockId)
        {
            Cache = &DeviceExtension->ClockRanges[Index];
            break;
        }
    }

    if (Cache && Cache->Queried)
    {
        if (!Cache->Valid)
            return STATUS_NOT_SUPPORTED;

        *MinimumRate = Cache->MinimumRate;
        *MaximumRate = Cache->MaximumRate;
        return STATUS_SUCCESS;
    }

    if (!Cache)
    {
        if (DeviceExtension->ClockRangeCount >= USBAUDIO_MAX_CLOCK_RANGES)
            return STATUS_INSUFFICIENT_RESOURCES;

        Cache = &DeviceExtension->ClockRanges[DeviceExtension->ClockRangeCount++];
        Cache->ClockId = ClockId;
        Cache->Queried = FALSE;
        Cache->Valid = FALSE;
    }

    RequestIndex = ((USHORT)ClockId << 8) | DeviceExtension->AudioControlInterfaceNumber;

    Status = UsbAudioGetSetPropertyTimeout(DeviceExtension->LowerDevice,
                                           USB_AUDIO2_GET_RANGE,
                                           USB_AUDIO2_CS_SAMPLING_FREQ << 8,
                                           RequestIndex,
                                           Header,
                                           sizeof(Header),
                                           USBD_TRANSFER_DIRECTION_IN,
                                           1500);

    Count = NT_SUCCESS(Status) ? (Header[0] | ((ULONG)Header[1] << 8)) : 1;
    if (!Count || Count > 16)
        Count = 1;

    Data = AllocFunction(2 + Count * 12);
    if (!Data)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = UsbAudioGetSetPropertyTimeout(DeviceExtension->LowerDevice,
                                           USB_AUDIO2_GET_RANGE,
                                           USB_AUDIO2_CS_SAMPLING_FREQ << 8,
                                           RequestIndex,
                                           Data,
                                           2 + Count * 12,
                                           USBD_TRANSFER_DIRECTION_IN,
                                           1500);
    Cache->Queried = TRUE;
    if (NT_SUCCESS(Status))
    {
        for (Index = 0; Index < Count; Index++)
        {
            ULONG SubMinimum = UsbAudioReadLittleEndian32(Data + 2 + Index * 12);
            ULONG SubMaximum = UsbAudioReadLittleEndian32(Data + 6 + Index * 12);

            if (SubMinimum && SubMinimum < Minimum)
                Minimum = SubMinimum;
            if (SubMaximum > Maximum)
                Maximum = SubMaximum;
        }
    }

    FreeFunction(Data);

    if (!NT_SUCCESS(Status) || !Maximum || Minimum > Maximum)
    {
        return STATUS_DEVICE_DATA_ERROR;
    }

    Cache->Valid = TRUE;
    Cache->MinimumRate = Minimum;
    Cache->MaximumRate = Maximum;
    *MinimumRate = Minimum;
    *MaximumRate = Maximum;
    return STATUS_SUCCESS;
}

ULONG
UsbAudioGetStreamingPacketRate(
    IN PDEVICE_EXTENSION DeviceExtension,
    IN PUSB_ENDPOINT_DESCRIPTOR EndpointDescriptor)
{
    return UacPacketRate(DeviceExtension->HighSpeed, EndpointDescriptor->bInterval);
}

static
VOID
UsbAudioGetDataRangesUac2(
    IN PDEVICE_EXTENSION DeviceExtension,
    IN UCHAR bTerminalID,
    OUT PKSDATARANGE** OutDataRanges,
    OUT PULONG OutDataRangesCount)
{
    PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor;
    PUSB_AUDIO2_STREAMING_INTERFACE_DESCRIPTOR StreamingInterfaceDescriptor;
    PUSB_AUDIO2_STREAMING_FORMAT_TYPE_DESCRIPTOR FormatDescriptor;
    PUSB_INTERFACE_DESCRIPTOR Descriptor;
    PUSB_ENDPOINT_DESCRIPTOR EndpointDescriptor;
    PKSDATARANGE_AUDIO DataRangeAudio;
    PKSDATARANGE *DataRangeAudioArray = NULL;
    ULONG ClockId;
    ULONG Count = 0;
    ULONG Index = 0;
    ULONG MinimumRate;
    ULONG MaximumRate;
    ULONG Rate;
    ULONG Offset;

    *OutDataRanges = NULL;
    *OutDataRangesCount = 0;

    ConfigurationDescriptor = DeviceExtension->ConfigurationDescriptor;
    ClockId = UsbAudioGetTerminalClockId(DeviceExtension, bTerminalID);

    for (Offset = 0; Offset + sizeof(USB_COMMON_DESCRIPTOR) <= ConfigurationDescriptor->wTotalLength; )
    {
        Descriptor = (PUSB_INTERFACE_DESCRIPTOR)((PUCHAR)ConfigurationDescriptor + Offset);

        if (Descriptor->bLength < sizeof(USB_COMMON_DESCRIPTOR) ||
            Offset + Descriptor->bLength > ConfigurationDescriptor->wTotalLength)
            break;

        Offset += Descriptor->bLength;

        if (Descriptor->bDescriptorType != USB_INTERFACE_DESCRIPTOR_TYPE ||
            Descriptor->bInterfaceClass != USB_DEVICE_CLASS_AUDIO ||
            Descriptor->bInterfaceSubClass != 0x02)
            continue;
        if (Descriptor->bNumEndpoints == 0)
            continue;

        StreamingInterfaceDescriptor = (PUSB_AUDIO2_STREAMING_INTERFACE_DESCRIPTOR)
            UsbAudioFindStreamingClassDescriptor(ConfigurationDescriptor, Descriptor, 0x01);
        if (!StreamingInterfaceDescriptor ||
            StreamingInterfaceDescriptor->bLength < sizeof(USB_AUDIO2_STREAMING_INTERFACE_DESCRIPTOR) ||
            StreamingInterfaceDescriptor->bTerminalLink != bTerminalID ||
            StreamingInterfaceDescriptor->bFormatType != USB_AUDIO2_FORMAT_TYPE_I ||
            !(StreamingInterfaceDescriptor->bmFormats & USB_AUDIO2_FORMAT_PCM))
        {
            continue;
        }

        FormatDescriptor = (PUSB_AUDIO2_STREAMING_FORMAT_TYPE_DESCRIPTOR)
            UsbAudioFindStreamingClassDescriptor(ConfigurationDescriptor, Descriptor, 0x02);
        if (!FormatDescriptor ||
            FormatDescriptor->bLength < sizeof(USB_AUDIO2_STREAMING_FORMAT_TYPE_DESCRIPTOR) ||
            FormatDescriptor->bFormatType != USB_AUDIO2_FORMAT_TYPE_I ||
            !FormatDescriptor->bSubslotSize ||
            !StreamingInterfaceDescriptor->bNrChannels)
        {
            continue;
        }

        Count++;
    }

    if (!Count)
    {
        DPRINT1("UsbAudioGetDataRangesUac2: terminal %u has no UAC2 format\n", bTerminalID);
        return;
    }

    DataRangeAudioArray = AllocFunction(sizeof(PVOID) * Count);
    if (!DataRangeAudioArray)
        return;

    for (Offset = 0; Offset + sizeof(USB_COMMON_DESCRIPTOR) <= ConfigurationDescriptor->wTotalLength; )
    {
        Descriptor = (PUSB_INTERFACE_DESCRIPTOR)((PUCHAR)ConfigurationDescriptor + Offset);

        if (Descriptor->bLength < sizeof(USB_COMMON_DESCRIPTOR) ||
            Offset + Descriptor->bLength > ConfigurationDescriptor->wTotalLength)
            break;

        Offset += Descriptor->bLength;

        if (Descriptor->bDescriptorType != USB_INTERFACE_DESCRIPTOR_TYPE ||
            Descriptor->bInterfaceClass != USB_DEVICE_CLASS_AUDIO ||
            Descriptor->bInterfaceSubClass != 0x02)
            continue;
        if (Descriptor->bNumEndpoints == 0)
            continue;

        StreamingInterfaceDescriptor = (PUSB_AUDIO2_STREAMING_INTERFACE_DESCRIPTOR)
            UsbAudioFindStreamingClassDescriptor(ConfigurationDescriptor, Descriptor, 0x01);
        if (!StreamingInterfaceDescriptor ||
            StreamingInterfaceDescriptor->bLength < sizeof(USB_AUDIO2_STREAMING_INTERFACE_DESCRIPTOR) ||
            StreamingInterfaceDescriptor->bTerminalLink != bTerminalID ||
            StreamingInterfaceDescriptor->bFormatType != USB_AUDIO2_FORMAT_TYPE_I ||
            !(StreamingInterfaceDescriptor->bmFormats & USB_AUDIO2_FORMAT_PCM))
        {
            continue;
        }

        FormatDescriptor = (PUSB_AUDIO2_STREAMING_FORMAT_TYPE_DESCRIPTOR)
            UsbAudioFindStreamingClassDescriptor(ConfigurationDescriptor, Descriptor, 0x02);
        if (!FormatDescriptor ||
            FormatDescriptor->bLength < sizeof(USB_AUDIO2_STREAMING_FORMAT_TYPE_DESCRIPTOR) ||
            FormatDescriptor->bFormatType != USB_AUDIO2_FORMAT_TYPE_I ||
            !FormatDescriptor->bSubslotSize ||
            !StreamingInterfaceDescriptor->bNrChannels)
        {
            continue;
        }

        MinimumRate = 0;
        MaximumRate = 0;
        if (!ClockId ||
            !NT_SUCCESS(UsbAudioQueryClockRange(DeviceExtension, (UCHAR)ClockId, &MinimumRate, &MaximumRate)))
        {
            EndpointDescriptor = (PUSB_ENDPOINT_DESCRIPTOR)
                USBD_ParseDescriptors(ConfigurationDescriptor,
                                      ConfigurationDescriptor->wTotalLength,
                                      Descriptor,
                                      USB_ENDPOINT_DESCRIPTOR_TYPE);
            if (!EndpointDescriptor)
                continue;

            Rate = UsbAudioGetStreamingPacketRate(DeviceExtension, EndpointDescriptor);
            MaximumRate = (EndpointDescriptor->wMaxPacketSize /
                           (FormatDescriptor->bSubslotSize * StreamingInterfaceDescriptor->bNrChannels)) *
                          Rate;
            MinimumRate = Rate;
            DPRINT1("UsbAudioGetDataRangesUac2: clock %u range unavailable, using %lu-%lu Hz\n",
                    ClockId, MinimumRate, MaximumRate);
        }

        /* a data endpoint that cannot carry the rate would starve the ring */
        {
            PUSB_ENDPOINT_DESCRIPTOR DataEndpoint = NULL;
            PUCHAR Cursor = (PUCHAR)Descriptor + Descriptor->bLength;
            PUCHAR End = (PUCHAR)ConfigurationDescriptor + ConfigurationDescriptor->wTotalLength;

            while (Cursor + sizeof(USB_COMMON_DESCRIPTOR) <= End)
            {
                PUSB_COMMON_DESCRIPTOR Common = (PUSB_COMMON_DESCRIPTOR)Cursor;

                if (Common->bLength < sizeof(USB_COMMON_DESCRIPTOR) ||
                    Cursor + Common->bLength > End)
                    break;

                if (Common->bDescriptorType == USB_INTERFACE_DESCRIPTOR_TYPE)
                    break;

                if (Common->bDescriptorType == USB_ENDPOINT_DESCRIPTOR_TYPE)
                {
                    PUSB_ENDPOINT_DESCRIPTOR Endpoint = (PUSB_ENDPOINT_DESCRIPTOR)Common;

                    if ((Endpoint->bmAttributes & 0x03) == 0x01)
                    {
                        DataEndpoint = Endpoint;
                        break;
                    }
                }

                Cursor += Common->bLength;
            }

            if (DataEndpoint)
            {
                ULONG MinimumPacket;

                MinimumPacket = UacDescMinDataWMaxPacket(DeviceExtension->HighSpeed,
                                                         DataEndpoint->bInterval,
                                                         MaximumRate,
                                                         StreamingInterfaceDescriptor->bNrChannels,
                                                         FormatDescriptor->bSubslotSize);
                if (MinimumPacket &&
                    (ULONG)(DataEndpoint->wMaxPacketSize & 0x07FF) < MinimumPacket)
                {
                    continue;
                }
            }
        }

        if (!MaximumRate || MinimumRate > MaximumRate)
            continue;

        DataRangeAudio = AllocFunction(sizeof(KSDATARANGE_AUDIO));
        if (!DataRangeAudio)
            break;

        DataRangeAudio->DataRange.FormatSize = sizeof(KSDATARANGE_AUDIO);
        DataRangeAudio->DataRange.MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
        DataRangeAudio->DataRange.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
        DataRangeAudio->DataRange.Specifier = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;
        DataRangeAudio->DataRange.SampleSize =
            FormatDescriptor->bSubslotSize * StreamingInterfaceDescriptor->bNrChannels;
        DataRangeAudio->MaximumChannels = StreamingInterfaceDescriptor->bNrChannels;
        DataRangeAudio->MinimumBitsPerSample = FormatDescriptor->bBitResolution;
        DataRangeAudio->MaximumBitsPerSample = FormatDescriptor->bBitResolution;
        DataRangeAudio->MinimumSampleFrequency = MinimumRate;
        DataRangeAudio->MaximumSampleFrequency = MaximumRate;

        DataRangeAudioArray[Index] = (PKSDATARANGE)DataRangeAudio;
        Index++;
    }

    *OutDataRanges = DataRangeAudioArray;
    *OutDataRangesCount = Index;
    DPRINT1("UsbAudioGetDataRangesUac2: terminal %u clock %u ranges=%lu\n",
            bTerminalID, ClockId, Index);
}

static
VOID
UsbAudioGetDataRangesUac1(
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    IN UCHAR bTerminalID,
    OUT PKSDATARANGE** OutDataRanges,
    OUT PULONG OutDataRangesCount)
{
    PUSB_AUDIO_STREAMING_INTERFACE_DESCRIPTOR StreamingInterfaceDescriptor;
    PUSB_AUDIO_STREAMING_FORMAT_TYPE_DESCRIPTOR StreamingFormatDescriptor;
    PUSB_INTERFACE_DESCRIPTOR Descriptor;
    PKSDATARANGE_AUDIO DataRangeAudio;
    PKSDATARANGE *DataRangeAudioArray = NULL;
    ULONG NumFrequency, DataRangeCount, DataRangeIndex, Index, Range;

    *OutDataRanges = NULL;
    *OutDataRangesCount = 0;

    /* count all data ranges */
    DataRangeCount = 0;
    for (Descriptor = USBD_ParseConfigurationDescriptorEx(ConfigurationDescriptor, ConfigurationDescriptor, -1, -1, USB_DEVICE_CLASS_AUDIO, -1, -1);
    Descriptor != NULL;
        Descriptor = USBD_ParseConfigurationDescriptorEx(ConfigurationDescriptor, (PVOID)((ULONG_PTR)Descriptor + Descriptor->bLength), -1, -1, USB_DEVICE_CLASS_AUDIO, -1, -1))
    {
        if (Descriptor->bInterfaceSubClass == 0x02 && /* AUDIO_STREAMING */
            Descriptor->bNumEndpoints > 0)
        {
            StreamingInterfaceDescriptor = (PUSB_AUDIO_STREAMING_INTERFACE_DESCRIPTOR)USBD_ParseDescriptors(ConfigurationDescriptor, ConfigurationDescriptor->wTotalLength, Descriptor, USB_AUDIO_CONTROL_TERMINAL_DESCRIPTOR_TYPE);
            if (StreamingInterfaceDescriptor != NULL &&
                StreamingInterfaceDescriptor->bDescriptorSubtype == 0x01 &&
                StreamingInterfaceDescriptor->wFormatTag == WAVE_FORMAT_PCM)
            {
                if (StreamingInterfaceDescriptor->bTerminalLink == bTerminalID)
                {
                    StreamingFormatDescriptor = (PUSB_AUDIO_STREAMING_FORMAT_TYPE_DESCRIPTOR)((ULONG_PTR)StreamingInterfaceDescriptor + StreamingInterfaceDescriptor->bLength);
                    if (StreamingFormatDescriptor->bDescriptorType != USB_AUDIO_CONTROL_TERMINAL_DESCRIPTOR_TYPE ||
                        StreamingFormatDescriptor->bDescriptorSubtype != 0x02 ||
                        StreamingFormatDescriptor->bFormatType != 0x01 ||
                        StreamingFormatDescriptor->bSubframeSize == 0 ||
                        StreamingFormatDescriptor->bNrChannels == 0)
                    {
                        continue;
                    }

                    NumFrequency = StreamingFormatDescriptor->bSamFreqType;
                    if (NumFrequency == 0)
                    {
                        if (StreamingFormatDescriptor->bLength >=
                            FIELD_OFFSET(USB_AUDIO_STREAMING_FORMAT_TYPE_DESCRIPTOR, tSamFreq) + 6)
                        {
                            DataRangeCount++;
                        }
                    }
                    else if (StreamingFormatDescriptor->bLength >=
                             FIELD_OFFSET(USB_AUDIO_STREAMING_FORMAT_TYPE_DESCRIPTOR, tSamFreq) +
                             (NumFrequency * 3))
                    {
                        DataRangeCount += NumFrequency;
                    }
                }
            }
        }
    }

    if (DataRangeCount == 0)
        return;

    DataRangeAudioArray = AllocFunction(sizeof(PVOID) * DataRangeCount);
    if (DataRangeAudioArray == NULL)
    {
        /* no memory */
        return;
    }

    DataRangeIndex = 0;
    for (Descriptor = USBD_ParseConfigurationDescriptorEx(ConfigurationDescriptor, ConfigurationDescriptor, -1, -1, USB_DEVICE_CLASS_AUDIO, -1, -1);
    Descriptor != NULL;
        Descriptor = USBD_ParseConfigurationDescriptorEx(ConfigurationDescriptor, (PVOID)((ULONG_PTR)Descriptor + Descriptor->bLength), -1, -1, USB_DEVICE_CLASS_AUDIO, -1, -1))
    {
        if (Descriptor->bInterfaceSubClass == 0x02 && /* AUDIO_STREAMING */
            Descriptor->bNumEndpoints > 0)
        {
            StreamingInterfaceDescriptor = (PUSB_AUDIO_STREAMING_INTERFACE_DESCRIPTOR)USBD_ParseDescriptors(ConfigurationDescriptor, ConfigurationDescriptor->wTotalLength, Descriptor, USB_AUDIO_CONTROL_TERMINAL_DESCRIPTOR_TYPE);
            if (StreamingInterfaceDescriptor != NULL &&
                StreamingInterfaceDescriptor->bDescriptorSubtype == 0x01 &&
                StreamingInterfaceDescriptor->wFormatTag == WAVE_FORMAT_PCM)
            {
                if (StreamingInterfaceDescriptor->bTerminalLink == bTerminalID)
                {
                    StreamingFormatDescriptor = (PUSB_AUDIO_STREAMING_FORMAT_TYPE_DESCRIPTOR)((ULONG_PTR)StreamingInterfaceDescriptor + StreamingInterfaceDescriptor->bLength);
                    if (StreamingFormatDescriptor->bDescriptorType != 0x24 ||
                        StreamingFormatDescriptor->bDescriptorSubtype != 0x02 ||
                        StreamingFormatDescriptor->bFormatType != 0x01)
                    {
                        continue;
                    }

                    NumFrequency = StreamingFormatDescriptor->bSamFreqType;
                    if (NumFrequency == 0)
                    {
                        if (StreamingFormatDescriptor->bLength <
                            FIELD_OFFSET(USB_AUDIO_STREAMING_FORMAT_TYPE_DESCRIPTOR, tSamFreq) + 6)
                        {
                            continue;
                        }

                        DataRangeAudio = AllocFunction(sizeof(KSDATARANGE_AUDIO));
                        if (DataRangeAudio == NULL)
                            goto Cleanup;

                        DataRangeAudio->DataRange.FormatSize = sizeof(KSDATARANGE_AUDIO);
                        DataRangeAudio->DataRange.MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
                        DataRangeAudio->DataRange.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
                        DataRangeAudio->DataRange.Specifier = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;
                        DataRangeAudio->DataRange.SampleSize =
                            StreamingFormatDescriptor->bSubframeSize *
                            StreamingFormatDescriptor->bNrChannels;
                        DataRangeAudio->MaximumChannels = StreamingFormatDescriptor->bNrChannels;
                        DataRangeAudio->MinimumBitsPerSample = StreamingFormatDescriptor->bBitResolution;
                        DataRangeAudio->MaximumBitsPerSample = StreamingFormatDescriptor->bBitResolution;
                        DataRangeAudio->MinimumSampleFrequency =
                            UsbAudioReadSampleFrequency(&StreamingFormatDescriptor->tSamFreq[0]);
                        DataRangeAudio->MaximumSampleFrequency =
                            UsbAudioReadSampleFrequency(StreamingFormatDescriptor->tSamFreq + 3);

                        DataRangeAudioArray[DataRangeIndex] = (PKSDATARANGE)DataRangeAudio;
                        DataRangeIndex++;
                    }
                    else
                    {
                        for (Index = 0; Index < NumFrequency; Index++)
                        {
                            ULONG SampleFrequency;

                            if (StreamingFormatDescriptor->bLength <
                                FIELD_OFFSET(USB_AUDIO_STREAMING_FORMAT_TYPE_DESCRIPTOR, tSamFreq) +
                                ((Index + 1) * 3))
                            {
                                break;
                            }

                            SampleFrequency =
                                UsbAudioReadSampleFrequency(&StreamingFormatDescriptor->tSamFreq[Index * 3]);

                            DataRangeAudio = AllocFunction(sizeof(KSDATARANGE_AUDIO));
                            if (DataRangeAudio == NULL)
                                goto Cleanup;

                            DataRangeAudio->DataRange.FormatSize = sizeof(KSDATARANGE_AUDIO);
                            DataRangeAudio->DataRange.MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
                            DataRangeAudio->DataRange.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
                            DataRangeAudio->DataRange.Specifier = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;
                            DataRangeAudio->DataRange.SampleSize =
                                StreamingFormatDescriptor->bSubframeSize *
                                StreamingFormatDescriptor->bNrChannels;
                            DataRangeAudio->MaximumChannels = StreamingFormatDescriptor->bNrChannels;
                            DataRangeAudio->MinimumBitsPerSample = StreamingFormatDescriptor->bBitResolution;
                            DataRangeAudio->MaximumBitsPerSample = StreamingFormatDescriptor->bBitResolution;
                            DataRangeAudio->MinimumSampleFrequency = SampleFrequency;
                            DataRangeAudio->MaximumSampleFrequency = SampleFrequency;

                            DataRangeAudioArray[DataRangeIndex] = (PKSDATARANGE)DataRangeAudio;
                            DataRangeIndex++;
                        }
                    }
                }
            }
        }
    }

    if (DataRangeIndex == 0)
    {
        goto Cleanup;
    }

    *OutDataRanges = DataRangeAudioArray;
    *OutDataRangesCount = DataRangeIndex;
    return;

Cleanup:
    for (Range = 0; Range < DataRangeIndex; Range++)
    {
        if (DataRangeAudioArray[Range])
            FreeFunction(DataRangeAudioArray[Range]);
    }
    if (DataRangeAudioArray)
        FreeFunction(DataRangeAudioArray);
}

static
VOID
UsbAudioGetDataRanges(
    IN PDEVICE_EXTENSION DeviceExtension,
    IN UCHAR bTerminalID,
    OUT PKSDATARANGE** OutDataRanges,
    OUT PULONG OutDataRangesCount)
{
    if (DeviceExtension->AudioVersion >= USB_AUDIO_VERSION_2)
    {
        UsbAudioGetDataRangesUac2(DeviceExtension,
                                  bTerminalID,
                                  OutDataRanges,
                                  OutDataRangesCount);
        return;
    }

    UsbAudioGetDataRangesUac1(DeviceExtension->ConfigurationDescriptor,
                              bTerminalID,
                              OutDataRanges,
                              OutDataRangesCount);
}


NTSTATUS
USBAudioPinBuildDescriptors(
    PKSDEVICE Device,
    PKSPIN_DESCRIPTOR_EX *PinDescriptors,
    PULONG PinDescriptorsCount,
    PULONG PinDescriptorSize)
{
    PDEVICE_EXTENSION DeviceExtension;
    PKSPIN_DESCRIPTOR_EX Pins;
    ULONG TotalTerminalDescriptorCount = 0;
    ULONG NonStreamingTerminalDescriptorCount = 0;
    ULONG StreamingTerminalDescriptorCount = 0;
    ULONG RenderBridgePinCount = 0;
    ULONG TotalPinCount = 0;
    ULONG Index = 0;
    ULONG PinOffset;
    PUSB_AUDIO_CONTROL_INPUT_TERMINAL_DESCRIPTOR TerminalDescriptor = NULL;

    /* get device extension */
    DeviceExtension = Device->Context;

    *PinDescriptors = NULL;
    *PinDescriptorsCount = 0;
    *PinDescriptorSize = sizeof(KSPIN_DESCRIPTOR_EX);

    CountTerminalUnits(DeviceExtension->ConfigurationDescriptor, &NonStreamingTerminalDescriptorCount, &TotalTerminalDescriptorCount);
    DPRINT("TotalTerminalDescriptorCount %lu NonStreamingTerminalDescriptorCount %lu\n", TotalTerminalDescriptorCount, NonStreamingTerminalDescriptorCount);

    if (TotalTerminalDescriptorCount == 0 ||
        NonStreamingTerminalDescriptorCount > TotalTerminalDescriptorCount)
    {
        return STATUS_NOT_SUPPORTED;
    }

    StreamingTerminalDescriptorCount = TotalTerminalDescriptorCount - NonStreamingTerminalDescriptorCount;
    RenderBridgePinCount = UsbAudioCountRenderBridgePins(DeviceExtension->ConfigurationDescriptor);
    if (RenderBridgePinCount > MAXULONG - TotalTerminalDescriptorCount)
        return STATUS_INTEGER_OVERFLOW;

    TotalPinCount = TotalTerminalDescriptorCount + RenderBridgePinCount;
    if (TotalPinCount > MAXULONG / sizeof(KSPIN_DESCRIPTOR_EX))
        return STATUS_INTEGER_OVERFLOW;

    /* allocate pins */
    Pins = AllocFunction(sizeof(KSPIN_DESCRIPTOR_EX) * TotalPinCount);
    if (!Pins)
    {
        /* no memory*/
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (Index = 0; Index < TotalPinCount; Index++)
    {
        if (Index < StreamingTerminalDescriptorCount)
        {
            /* irp sink pins*/
            TerminalDescriptor = UsbAudioGetStreamingTerminalDescriptorByIndex(DeviceExtension->ConfigurationDescriptor, Index);
            if (!TerminalDescriptor)
            {
                DPRINT1("USBAudioPinBuildDescriptors: missing streaming terminal %lu\n", Index);
                FreeFunction(Pins);
                return STATUS_INVALID_DEVICE_REQUEST;
            }

            Pins[Index].Dispatch = &UsbAudioPinDispatch;
            Pins[Index].PinDescriptor.InterfacesCount = 1;
            Pins[Index].PinDescriptor.Interfaces = &StandardPinInterface;
            Pins[Index].PinDescriptor.MediumsCount = 1;
            Pins[Index].PinDescriptor.Mediums = &StandardPinMedium;
            Pins[Index].PinDescriptor.Category = UsbAudioGetPinCategoryFromTerminalDescriptor(TerminalDescriptor);
            UsbAudioGetDataRanges(DeviceExtension, TerminalDescriptor->bTerminalID, (PKSDATARANGE**)&Pins[Index].PinDescriptor.DataRanges, &Pins[Index].PinDescriptor.DataRangesCount);

            if (TerminalDescriptor->bDescriptorSubtype == USB_AUDIO_OUTPUT_TERMINAL)
            {
                Pins[Index].PinDescriptor.Communication = KSPIN_COMMUNICATION_BOTH;
                Pins[Index].PinDescriptor.DataFlow = KSPIN_DATAFLOW_OUT;

                /* pin flags */
                Pins[Index].Flags = KSPIN_FLAG_PROCESS_IN_RUN_STATE_ONLY | KSFILTER_FLAG_CRITICAL_PROCESSING;
            }
            else if (TerminalDescriptor->bDescriptorSubtype == USB_AUDIO_INPUT_TERMINAL)
            {
                Pins[Index].PinDescriptor.Communication = KSPIN_COMMUNICATION_SINK;
                Pins[Index].PinDescriptor.DataFlow = KSPIN_DATAFLOW_IN;

                /* pin flags */
                Pins[Index].Flags = KSPIN_FLAG_PROCESS_IN_RUN_STATE_ONLY | KSPIN_FLAG_GENERATE_EOS_EVENTS;
            }

            /* data intersect handler */
            Pins[Index].IntersectHandler = UsbAudioPinDataIntersect;

            /* irp sinks / sources can be instantiated */
            Pins[Index].InstancesPossible = 1;
        }
        else if (Index < StreamingTerminalDescriptorCount + RenderBridgePinCount)
        {
            TerminalDescriptor = UsbAudioGetRenderBridgeTerminalDescriptorByIndex(DeviceExtension->ConfigurationDescriptor,
                                                                                 Index - StreamingTerminalDescriptorCount);
            if (!TerminalDescriptor)
            {
                DPRINT1("USBAudioPinBuildDescriptors: missing render bridge terminal %lu\n",
                        Index - StreamingTerminalDescriptorCount);
                FreeFunction(Pins);
                return STATUS_INVALID_DEVICE_REQUEST;
            }

            Pins[Index].PinDescriptor.InterfacesCount = 1;
            Pins[Index].PinDescriptor.Interfaces = &StandardPinInterface;
            Pins[Index].PinDescriptor.MediumsCount = 1;
            Pins[Index].PinDescriptor.Mediums = &StandardPinMedium;
            Pins[Index].PinDescriptor.DataRanges = BridgePinAudioFormats;
            Pins[Index].PinDescriptor.DataRangesCount = 1;
            Pins[Index].PinDescriptor.Communication = KSPIN_COMMUNICATION_BRIDGE;
            Pins[Index].PinDescriptor.DataFlow = KSPIN_DATAFLOW_IN;
            Pins[Index].PinDescriptor.Category = UsbAudioGetPinCategoryFromTerminalDescriptor(TerminalDescriptor);
        }
        else
        {
            /* bridge pins */
            PinOffset = Index - (StreamingTerminalDescriptorCount + RenderBridgePinCount);
            TerminalDescriptor = UsbAudioGetNonStreamingTerminalDescriptorByIndex(DeviceExtension->ConfigurationDescriptor, PinOffset);
            if (!TerminalDescriptor)
            {
                DPRINT1("USBAudioPinBuildDescriptors: missing non-streaming terminal %lu\n",
                        PinOffset);
                FreeFunction(Pins);
                return STATUS_INVALID_DEVICE_REQUEST;
            }
            Pins[Index].PinDescriptor.InterfacesCount = 1;
            Pins[Index].PinDescriptor.Interfaces = &StandardPinInterface;
            Pins[Index].PinDescriptor.MediumsCount = 1;
            Pins[Index].PinDescriptor.Mediums = &StandardPinMedium;
            Pins[Index].PinDescriptor.DataRanges = BridgePinAudioFormats;
            Pins[Index].PinDescriptor.DataRangesCount = 1;
            Pins[Index].PinDescriptor.Communication = KSPIN_COMMUNICATION_BRIDGE;
            Pins[Index].PinDescriptor.Category = UsbAudioGetPinCategoryFromTerminalDescriptor(TerminalDescriptor);

            if (TerminalDescriptor->bDescriptorSubtype == USB_AUDIO_INPUT_TERMINAL)
            {
                Pins[Index].PinDescriptor.DataFlow = KSPIN_DATAFLOW_IN;
            }
            else if (TerminalDescriptor->bDescriptorSubtype == USB_AUDIO_OUTPUT_TERMINAL)
            {
                Pins[Index].PinDescriptor.DataFlow = KSPIN_DATAFLOW_OUT;
            }
        }

    }

    *PinDescriptors = Pins;
    *PinDescriptorSize = sizeof(KSPIN_DESCRIPTOR_EX);
    *PinDescriptorsCount = TotalPinCount;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
USBAudioGetDescriptor(
    IN PDEVICE_OBJECT DeviceObject,
    IN UCHAR DescriptorType,
    IN ULONG DescriptorLength,
    IN UCHAR DescriptorIndex,
    IN LANGID LanguageId,
    OUT PVOID *OutDescriptor)
{
    PURB Urb;
    NTSTATUS Status;
    PVOID Descriptor;

    /* sanity checks */
    ASSERT(DeviceObject);
    ASSERT(OutDescriptor);
    ASSERT(DescriptorLength);

    //
    // first allocate descriptor buffer
    //
    Descriptor = AllocFunction(DescriptorLength);
    if (!Descriptor)
    {
        /* no memory */
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* allocate urb */
    Urb = (PURB)AllocFunction(sizeof(URB));
    if (!Urb)
    {
        /* no memory */
        FreeFunction(Descriptor);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* initialize urb */
    UsbBuildGetDescriptorRequest(Urb,
        sizeof(Urb->UrbControlDescriptorRequest),
        DescriptorType,
        DescriptorIndex,
        LanguageId,
        Descriptor,
        NULL,
        DescriptorLength,
        NULL);

    /* submit urb */
    Status = SubmitUrbSync(DeviceObject, Urb);

    /* free urb */
    FreeFunction(Urb);

    if (NT_SUCCESS(Status))
    {
        /* store result */
        *OutDescriptor = Descriptor;
    }
    else
    {
        /* failed */
        FreeFunction(Descriptor);
    }

    /* done */
    return Status;
}

NTSTATUS
NTAPI
USBAudioGetStringDescriptor(
    IN PDEVICE_OBJECT DeviceObject,
    IN ULONG DescriptorLength,
    IN UCHAR DescriptorIndex,
    IN LANGID LanguageId,
    OUT PVOID *OutDescriptor)
{
    NTSTATUS Status;
    PUSB_STRING_DESCRIPTOR StringDescriptor;
    ULONG StringBytes, Size;
    PWCHAR String;

    /* retrieve descriptor */
    Status = USBAudioGetDescriptor(DeviceObject, USB_STRING_DESCRIPTOR_TYPE, DescriptorLength, DescriptorIndex, LanguageId, OutDescriptor);
    if (!NT_SUCCESS(Status))
    {
        // failed
        return Status;
    }

    StringDescriptor = (PUSB_STRING_DESCRIPTOR)*OutDescriptor;
    if (StringDescriptor->bLength < FIELD_OFFSET(USB_STRING_DESCRIPTOR, bString) ||
        StringDescriptor->bLength > DescriptorLength ||
        StringDescriptor->bDescriptorType != USB_STRING_DESCRIPTOR_TYPE ||
        ((StringDescriptor->bLength - FIELD_OFFSET(USB_STRING_DESCRIPTOR, bString)) & 1))
    {
        FreeFunction(StringDescriptor);
        *OutDescriptor = NULL;
        return STATUS_DEVICE_DATA_ERROR;
    }

    StringBytes = StringDescriptor->bLength - FIELD_OFFSET(USB_STRING_DESCRIPTOR, bString);
    Size = StringBytes + sizeof(WCHAR);

    String = AllocFunction(Size);
    if (String == NULL)
    {
        FreeFunction(StringDescriptor);
        *OutDescriptor = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(String, StringDescriptor->bString, StringBytes);
    String[StringBytes / sizeof(WCHAR)] = UNICODE_NULL;
    FreeFunction(StringDescriptor);

    *OutDescriptor = String;
    return STATUS_SUCCESS;
}

NTSTATUS
USBAudioRegCreateMediaCategoriesKey(
    IN PUNICODE_STRING Name,
    OUT PHANDLE OutHandle)
{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING DestinationString;
    HANDLE Handle;

    /* initialize root name*/
    RtlInitUnicodeString(&DestinationString, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\MediaCategories\\");

    /* initialize object attributes */
    InitializeObjectAttributes(&ObjectAttributes, &DestinationString, OBJ_CASE_INSENSITIVE | OBJ_OPENIF | OBJ_KERNEL_HANDLE, NULL, NULL);

    /* create the key */
    Status = ZwOpenKey(&Handle, KEY_ALL_ACCESS, &ObjectAttributes);
    if (NT_SUCCESS(Status))
    {
        /* initialize object attributes */
        InitializeObjectAttributes(&ObjectAttributes, Name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, Handle, NULL);

        Status = ZwCreateKey(OutHandle, KEY_ALL_ACCESS, &ObjectAttributes, 0, NULL, 0, NULL);
        ZwClose(Handle);

    }
    return Status;
}


NTSTATUS
USBAudioInitComponentId(
    PKSDEVICE Device,
    IN PKSCOMPONENTID ComponentId)
{
    PDEVICE_EXTENSION DeviceExtension;
    NTSTATUS Status;
    LPWSTR DescriptionBuffer;
    UNICODE_STRING GuidString;
    UNICODE_STRING Name;
    HANDLE hKey;
    GUID TempGuid;

    /* get device extension */
    DeviceExtension = Device->Context;

    /* init component id */
    ComponentId->Component = KSCOMPONENTID_USBAUDIO;
    ComponentId->Version = HIBYTE(DeviceExtension->DeviceDescriptor->bcdDevice);
    ComponentId->Revision = LOBYTE(DeviceExtension->DeviceDescriptor->bcdDevice);

    INIT_USBAUDIO_MID(&ComponentId->Manufacturer, DeviceExtension->DeviceDescriptor->idVendor);
    INIT_USBAUDIO_PID(&ComponentId->Product, DeviceExtension->DeviceDescriptor->idProduct);
    INIT_USBAUDIO_PRODUCT_NAME(&TempGuid, DeviceExtension->DeviceDescriptor->idVendor, DeviceExtension->DeviceDescriptor->idProduct, 0);

    /* Query device string descriptor — try US English (0x0409) first,
     * then fall back to the first language from the device's language list */
    if (DeviceExtension->DeviceDescriptor->iProduct)
    {
        Status = USBAudioGetStringDescriptor(DeviceExtension->LowerDevice,
                                             100 * sizeof(WCHAR),
                                             DeviceExtension->DeviceDescriptor->iProduct,
                                             0x0409,
                                             (PVOID*)&DescriptionBuffer);
        if (!NT_SUCCESS(Status))
        {
            DescriptionBuffer = NULL;
        }
        if (NT_SUCCESS(Status))
        {
            Status = RtlStringFromGUID(&TempGuid, &GuidString);
            if (NT_SUCCESS(Status))
            {
                Status = USBAudioRegCreateMediaCategoriesKey(&GuidString, &hKey);
                if (NT_SUCCESS(Status))
                {
                    RtlInitUnicodeString(&Name, L"Name");
                    ZwSetValueKey(hKey, &Name, 0, REG_SZ, DescriptionBuffer, (wcslen(DescriptionBuffer) + 1) * sizeof(WCHAR));
                    ZwClose(hKey);

                    INIT_USBAUDIO_PRODUCT_NAME(&ComponentId->Name, DeviceExtension->DeviceDescriptor->idVendor, DeviceExtension->DeviceDescriptor->idProduct, 0);
                }
                RtlFreeUnicodeString(&GuidString);
            }
            FreeFunction(DescriptionBuffer);
        }
    }
    return STATUS_SUCCESS;
}


NTSTATUS
NTAPI
USBAudioCreateFilterContext(
    PKSDEVICE Device)
{
    PKSFILTER_DESCRIPTOR FilterDescriptor;
    PKSCOMPONENTID ComponentId;
    NTSTATUS Status;

    /* allocate descriptor */
    FilterDescriptor = AllocFunction(sizeof(KSFILTER_DESCRIPTOR));
    if (!FilterDescriptor)
    {
        /* no memory */
        return USBD_STATUS_INSUFFICIENT_RESOURCES;
    }

    /* init filter descriptor*/
    FilterDescriptor->Version = KSFILTER_DESCRIPTOR_VERSION;
    FilterDescriptor->Flags = 0;
    FilterDescriptor->ReferenceGuid = &KSNAME_Filter;
    FilterDescriptor->Dispatch = &USBAudioFilterDispatch;
    FilterDescriptor->CategoriesCount = RTL_NUMBER_OF(UsbAudioFilterCategories);
    FilterDescriptor->Categories = UsbAudioFilterCategories;

    /* init component id*/
    ComponentId = AllocFunction(sizeof(KSCOMPONENTID));
    if (!ComponentId)
    {
        /* no memory */
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Status = USBAudioInitComponentId(Device, ComponentId);
    if (!NT_SUCCESS(Status))
    {
        /* failed*/
        FreeFunction(ComponentId);
        return Status;
    }
    FilterDescriptor->ComponentId = ComponentId;

    /* build pin descriptors */
    Status = USBAudioPinBuildDescriptors(Device, (PKSPIN_DESCRIPTOR_EX *)&FilterDescriptor->PinDescriptors, &FilterDescriptor->PinDescriptorsCount, &FilterDescriptor->PinDescriptorSize);
    if (!NT_SUCCESS(Status))
    {
        /* failed*/
        FreeFunction(ComponentId);
        return Status;
    }

    /* build topology */
    Status = BuildUSBAudioFilterTopology(Device, FilterDescriptor);
    if (!NT_SUCCESS(Status))
    {
        /* failed*/
        FreeFunction(ComponentId);
        return Status;
    }

    /* lets create the filter */
    Status = KsCreateFilterFactory(Device->FunctionalDeviceObject, FilterDescriptor, ReferenceString, NULL, KSCREATE_ITEM_FREEONSTOP, NULL, NULL, NULL);

    return Status;
}
