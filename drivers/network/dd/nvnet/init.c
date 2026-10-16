/*
 * PROJECT:     ReactOS nVidia nForce Ethernet Controller Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Miniport initialization helper routines
 * COPYRIGHT:   Copyright 2021-2022 Dmitry Borisov <di.sean@protonmail.com>
 */

/* INCLUDES *******************************************************************/

#include "nvnet.h"

#define NDEBUG
#include "debug.h"

/* FUNCTIONS ******************************************************************/

static
CODE_SEG("PAGE")
VOID
QueryInteger(
    _In_ NDIS_HANDLE ConfigurationHandle,
    _In_ PCWSTR EntryName,
    _Out_ PULONG EntryContext,
    _In_ ULONG DefaultValue,
    _In_ ULONG Minimum,
    _In_ ULONG Maximum)
{
    NDIS_STATUS Status;
    UNICODE_STRING Keyword;
    PNDIS_CONFIGURATION_PARAMETER ConfigurationParameter;

    PAGED_CODE();

    NdisInitUnicodeString(&Keyword, EntryName);
    NdisReadConfiguration(&Status,
                          &ConfigurationParameter,
                          ConfigurationHandle,
                          &Keyword,
                          NdisParameterInteger);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("'%S' request failed\n", EntryName));

        *EntryContext = DefaultValue;
    }
    else
    {
        if (ConfigurationParameter->ParameterData.IntegerData >= Minimum &&
            ConfigurationParameter->ParameterData.IntegerData <= Maximum)
        {
            *EntryContext = ConfigurationParameter->ParameterData.IntegerData;
        }
        else
        {
            NDIS_DbgPrint(MAX_TRACE, ("'%S' value out of range\n", EntryName));

            *EntryContext = DefaultValue;
        }
    }

    NDIS_DbgPrint(MIN_TRACE, ("Set '%S' to %d\n", EntryName, *EntryContext));
}

static
CODE_SEG("PAGE")
NDIS_STATUS
NvNetReadConfiguration(
    _Inout_ PNVNET_ADAPTER Adapter)
{
    NDIS_STATUS Status;
    NDIS_HANDLE ConfigurationHandle;
    NDIS_CONFIGURATION_OBJECT ConfigurationObject;
    PUCHAR NetworkAddress;
    UINT Length;
    ULONG GenericUlong;

    PAGED_CODE();

    NDIS_DbgPrint(MIN_TRACE, ("()\n"));

    ConfigurationObject.Header.Type = NDIS_OBJECT_TYPE_CONFIGURATION_OBJECT;
    ConfigurationObject.Header.Revision = NDIS_CONFIGURATION_OBJECT_REVISION_1;
    ConfigurationObject.Header.Size = NDIS_SIZEOF_CONFIGURATION_OBJECT_REVISION_1;
    ConfigurationObject.NdisHandle = Adapter->AdapterHandle;
    ConfigurationObject.Flags = 0;

    Status = NdisOpenConfigurationEx(&ConfigurationObject, &ConfigurationHandle);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    QueryInteger(ConfigurationHandle,
                 L"OptimizationMode",
                 &GenericUlong,
                 NV_OPTIMIZATION_MODE_DYNAMIC,
                 NV_OPTIMIZATION_MODE_THROUGHPUT,
                 NV_OPTIMIZATION_MODE_DYNAMIC);
    Adapter->OptimizationMode = GenericUlong;

    QueryInteger(ConfigurationHandle,
                 L"FlowControl",
                 &GenericUlong,
                 NV_FLOW_CONTROL_AUTO,
                 NV_FLOW_CONTROL_DISABLE,
                 NV_FLOW_CONTROL_RX_TX);
    Adapter->FlowControlMode = GenericUlong;

    QueryInteger(ConfigurationHandle,
                 L"SpeedDuplex",
                 &GenericUlong,
                 0,
                 0,
                 4);
    switch (GenericUlong)
    {
        case 1:
            Adapter->Flags |= NV_FORCE_SPEED_AND_DUPLEX;
            break;
        case 2:
            Adapter->Flags |= (NV_FORCE_SPEED_AND_DUPLEX | NV_FORCE_FULL_DUPLEX);
            break;
        case 3:
            Adapter->Flags |= (NV_FORCE_SPEED_AND_DUPLEX | NV_USER_SPEED_100);
            break;
        case 4:
            Adapter->Flags |= (NV_FORCE_SPEED_AND_DUPLEX | NV_FORCE_FULL_DUPLEX |
                               NV_USER_SPEED_100);
            break;

        default:
            break;
    }

    QueryInteger(ConfigurationHandle,
                 L"ChecksumOffload",
                 &GenericUlong,
                 0,
                 0,
                 1);
    if (GenericUlong)
        Adapter->Flags |= NV_SEND_CHECKSUM;

    QueryInteger(ConfigurationHandle,
                 L"LargeSendOffload",
                 &GenericUlong,
                 0,
                 0,
                 1);
    if (GenericUlong)
        Adapter->Flags |= NV_SEND_LARGE_SEND;

    QueryInteger(ConfigurationHandle,
                 L"JumboSize",
                 &GenericUlong,
                 NVNET_MAXIMUM_FRAME_SIZE,
                 NVNET_MAXIMUM_FRAME_SIZE,
                 NVNET_MAXIMUM_FRAME_SIZE_JUMBO);
    Adapter->MaximumFrameSize = GenericUlong;

    QueryInteger(ConfigurationHandle,
                 L"Priority",
                 &GenericUlong,
                 0,
                 0,
                 1);
    if (GenericUlong)
        Adapter->Flags |= NV_PACKET_PRIORITY;

    QueryInteger(ConfigurationHandle,
                 L"VlanTag",
                 &GenericUlong,
                 0,
                 0,
                 1);
    if (GenericUlong)
        Adapter->Flags |= NV_VLAN_TAGGING;

    QueryInteger(ConfigurationHandle,
                 L"VlanID",
                 &GenericUlong,
                 0,
                 0,
                 NVNET_MAXIMUM_VLAN_ID);
    Adapter->VlanId = GenericUlong;

    NdisReadNetworkAddress(&Status,
                           (PVOID*)&NetworkAddress,
                           &Length,
                           ConfigurationHandle);
    if ((Status == NDIS_STATUS_SUCCESS) && (Length == ETH_LENGTH_OF_ADDRESS))
    {
        if ((ETH_IS_MULTICAST(NetworkAddress) || ETH_IS_BROADCAST(NetworkAddress)) ||
            !ETH_IS_LOCALLY_ADMINISTERED(NetworkAddress))
        {
            NDIS_DbgPrint(MAX_TRACE, ("Invalid software MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
                                      NetworkAddress[0],
                                      NetworkAddress[1],
                                      NetworkAddress[2],
                                      NetworkAddress[3],
                                      NetworkAddress[4],
                                      NetworkAddress[5]));
        }
        else
        {
            NDIS_DbgPrint(MIN_TRACE, ("Using software MAC\n"));

            ETH_COPY_NETWORK_ADDRESS(Adapter->CurrentMacAddress, NetworkAddress);

            Adapter->Flags |= NV_USE_SOFT_MAC_ADDRESS;
        }
    }
    Status = NDIS_STATUS_SUCCESS;

    NdisCloseConfiguration(ConfigurationHandle);

    return Status;
}

static
CODE_SEG("PAGE")
NDIS_STATUS
NvNetInitializeAdapterResources(
    _Inout_ PNVNET_ADAPTER Adapter,
    _In_opt_ PNDIS_RESOURCE_LIST AssignedResources)
{
    NDIS_STATUS Status;
    UINT i;

    PAGED_CODE();

    NDIS_DbgPrint(MIN_TRACE, ("()\n"));

    if (!AssignedResources)
    {
        NvNetLogError(Adapter, NDIS_ERROR_CODE_RESOURCE_CONFLICT);
        return NDIS_STATUS_RESOURCES;
    }

    for (i = 0; i < AssignedResources->Count; ++i)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor = &AssignedResources->PartialDescriptors[i];

        switch (Descriptor->Type)
        {
            case CmResourceTypeMemory:
            {
                Adapter->IoAddress = Descriptor->u.Memory.Start;
                Adapter->IoLength = Descriptor->u.Memory.Length;
                break;
            }

            case CmResourceTypeInterrupt:
            {
                Adapter->InterruptVector = Descriptor->u.Interrupt.Vector;
                Adapter->InterruptLevel = Descriptor->u.Interrupt.Level;
                Adapter->InterruptShared = (Descriptor->ShareDisposition == CmResourceShareShared);
                Adapter->InterruptFlags = Descriptor->Flags;
                break;
            }

            default:
                break;
        }
    }

    if (!Adapter->IoAddress.QuadPart || !Adapter->InterruptVector)
    {
        NvNetLogError(Adapter, NDIS_ERROR_CODE_RESOURCE_CONFLICT);
        return NDIS_STATUS_RESOURCES;
    }

    NDIS_DbgPrint(MIN_TRACE, ("MEM at [%I64X-%I64X]\n",
                              Adapter->IoAddress.QuadPart,
                              Adapter->IoAddress.QuadPart + Adapter->IoLength));
    NDIS_DbgPrint(MIN_TRACE, ("IRQ Vector %d Level %d\n",
                              Adapter->InterruptVector,
                              Adapter->InterruptLevel));

    Status = NdisMMapIoSpace((PVOID*)&Adapter->IoBase,
                             Adapter->AdapterHandle,
                             Adapter->IoAddress,
                             Adapter->IoLength);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NvNetLogError(Adapter, NDIS_ERROR_CODE_RESOURCE_CONFLICT);
    }

    return Status;
}

static
CODE_SEG("PAGE")
NDIS_STATUS
AllocateTransmitBuffers(
    _In_ PNVNET_ADAPTER Adapter)
{
    ULONG i;
    BOOLEAN HasBuffer = FALSE;
    PNVNET_TX_BUFFER CoalesceBuffer;
    NDIS_STATUS Status;

    PAGED_CODE();

    Status = NdisAllocateMemoryWithTag((PVOID*)&CoalesceBuffer,
                                       NVNET_TRANSMIT_BUFFERS * sizeof(NVNET_TX_BUFFER),
                                       NVNET_TAG);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    NdisZeroMemory(CoalesceBuffer, NVNET_TRANSMIT_BUFFERS * sizeof(NVNET_TX_BUFFER));

    Adapter->SendBuffer = CoalesceBuffer;

    for (i = 0; i < NVNET_TRANSMIT_BUFFERS; ++i)
    {
        PVOID VirtualAddress;
        NDIS_PHYSICAL_ADDRESS PhysicalAddress;

        NdisMAllocateSharedMemory(Adapter->AdapterHandle,
                                  Adapter->MaximumFrameSize + NVNET_ALIGNMENT,
                                  TRUE, /* Cached */
                                  &VirtualAddress,
                                  &PhysicalAddress);
        if (!VirtualAddress)
            continue;

        CoalesceBuffer->VirtualAddress = ALIGN_UP_POINTER_BY(VirtualAddress, NVNET_ALIGNMENT);
        CoalesceBuffer->PhysicalAddress.QuadPart =
            ALIGN_UP_BY(PhysicalAddress.QuadPart, NVNET_ALIGNMENT);

        Adapter->SendBufferAllocationData[i].PhysicalAddress.QuadPart = PhysicalAddress.QuadPart;
        Adapter->SendBufferAllocationData[i].VirtualAddress = VirtualAddress;

        PushEntryList(&Adapter->Send.BufferList, &CoalesceBuffer->Link);
        ++CoalesceBuffer;

        HasBuffer = TRUE;
    }
    if (!HasBuffer)
    {
        return NDIS_STATUS_RESOURCES;
    }

    return NDIS_STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NDIS_STATUS
AllocateTransmitBlocks(
    _In_ PNVNET_ADAPTER Adapter)
{
    PNVNET_TCB Tcb;
    NDIS_STATUS Status;

    PAGED_CODE();

    Status = NdisAllocateMemoryWithTag((PVOID*)&Tcb,
                                       NVNET_TRANSMIT_BLOCKS * sizeof(NVNET_TCB),
                                       NVNET_TAG);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    NdisZeroMemory(Tcb, NVNET_TRANSMIT_BLOCKS * sizeof(NVNET_TCB));

    Adapter->Send.TailTcb = Tcb + (NVNET_TRANSMIT_BLOCKS - 1);
    Adapter->Send.HeadTcb = Tcb;
    Adapter->Send.CurrentTcb = Tcb;
    Adapter->Send.LastTcb = Tcb;

    return NDIS_STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NDIS_STATUS
AllocateTransmitDescriptors(
    _In_ PNVNET_ADAPTER Adapter)
{
    NVNET_TBD Tbd;
    ULONG Size;

    PAGED_CODE();

    if (Adapter->Features & DEV_HAS_HIGH_DMA)
    {
        Size = sizeof(NVNET_DESCRIPTOR_64);
    }
    else
    {
        Size = sizeof(NVNET_DESCRIPTOR_32);
    }
    NdisMAllocateSharedMemory(Adapter->AdapterHandle,
                              Size * NVNET_TRANSMIT_DESCRIPTORS + NVNET_ALIGNMENT,
                              TRUE, /* Cached */
                              &Adapter->TbdOriginal,
                              &Adapter->TbdPhysOriginal);
    if (!Adapter->TbdOriginal)
        return NDIS_STATUS_RESOURCES;

    Tbd.Memory = ALIGN_UP_POINTER_BY(Adapter->TbdOriginal, NVNET_ALIGNMENT);
    Adapter->TbdPhys.QuadPart = ALIGN_UP_BY(Adapter->TbdPhysOriginal.QuadPart, NVNET_ALIGNMENT);

    Adapter->Send.HeadTbd = Tbd;
    Adapter->Send.CurrentTbd = Tbd;

    if (Adapter->Features & DEV_HAS_HIGH_DMA)
    {
        Adapter->Send.TailTbd.x64 = Tbd.x64 + (NVNET_TRANSMIT_DESCRIPTORS - 1);
    }
    else
    {
        Adapter->Send.TailTbd.x32 = Tbd.x32 + (NVNET_TRANSMIT_DESCRIPTORS - 1);
    }

    return NDIS_STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NDIS_STATUS
AllocateReceiveDescriptors(
    _In_ PNVNET_ADAPTER Adapter)
{
    ULONG Size;

    PAGED_CODE();

    if (Adapter->Features & DEV_HAS_HIGH_DMA)
    {
        Size = sizeof(NVNET_DESCRIPTOR_64);
    }
    else
    {
        Size = sizeof(NVNET_DESCRIPTOR_32);
    }
    NdisMAllocateSharedMemory(Adapter->AdapterHandle,
                              Size * NVNET_RECEIVE_DESCRIPTORS + NVNET_ALIGNMENT,
                              TRUE, /* Cached */
                              &Adapter->RbdOriginal,
                              &Adapter->RbdPhysOriginal);
    if (!Adapter->RbdOriginal)
        return NDIS_STATUS_RESOURCES;

    Adapter->Receive.NvRbd.Memory = ALIGN_UP_POINTER_BY(Adapter->RbdOriginal, NVNET_ALIGNMENT);
    Adapter->RbdPhys.QuadPart = ALIGN_UP_BY(Adapter->RbdPhysOriginal.QuadPart, NVNET_ALIGNMENT);

    return NDIS_STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NDIS_STATUS
AllocateReceiveBuffers(
    _In_ PNVNET_ADAPTER Adapter)
{
    PAGED_CODE();

    NdisMAllocateSharedMemory(Adapter->AdapterHandle,
                              NVNET_RECEIVE_BUFFER_SIZE * NVNET_RECEIVE_DESCRIPTORS,
                              TRUE, /* Cached */
                              (PVOID*)&Adapter->ReceiveBuffer,
                              &Adapter->ReceiveBufferPhys);
    if (!Adapter->ReceiveBuffer)
    {
        return NDIS_STATUS_RESOURCES;
    }

    return NDIS_STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NDIS_STATUS
AllocateReceiveNbls(
    _In_ PNVNET_ADAPTER Adapter)
{
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParameters;
    PNET_BUFFER_LIST Nbl;
    PVOID Buffer;
    PMDL Mdl;
    ULONG i;

    PAGED_CODE();

    NdisZeroMemory(&PoolParameters, sizeof(PoolParameters));
    PoolParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParameters.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.Header.Size = NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    PoolParameters.fAllocateNetBuffer = TRUE;
    PoolParameters.PoolTag = NVNET_TAG;

    Adapter->ReceiveNblPool = NdisAllocateNetBufferListPool(Adapter->AdapterHandle,
                                                            &PoolParameters);
    if (!Adapter->ReceiveNblPool)
        return NDIS_STATUS_RESOURCES;

    for (i = 0; i < NVNET_RECEIVE_NBL_COUNT; ++i)
    {
        Buffer = NdisAllocateMemoryWithTagPriority(Adapter->AdapterHandle,
                                                   NVNET_RECEIVE_BUFFER_SIZE,
                                                   NVNET_TAG,
                                                   NormalPoolPriority);
        if (!Buffer)
            return NDIS_STATUS_RESOURCES;

        Mdl = NdisAllocateMdl(Adapter->AdapterHandle, Buffer, NVNET_RECEIVE_BUFFER_SIZE);
        if (!Mdl)
        {
            NdisFreeMemory(Buffer, 0, 0);
            return NDIS_STATUS_RESOURCES;
        }

        Nbl = NdisAllocateNetBufferAndNetBufferList(Adapter->ReceiveNblPool,
                                                    0,
                                                    0,
                                                    Mdl,
                                                    0,
                                                    NVNET_RECEIVE_BUFFER_SIZE);
        if (!Nbl)
        {
            NdisFreeMdl(Mdl);
            NdisFreeMemory(Buffer, 0, 0);
            return NDIS_STATUS_RESOURCES;
        }

        Nbl->MiniportReserved[0] = Buffer;
        Nbl->SourceHandle = Adapter->AdapterHandle;
        Adapter->ReceiveNbls[i] = Nbl;
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = Adapter->FreeReceiveNbls;
        Adapter->FreeReceiveNbls = Nbl;
    }

    return NDIS_STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
VOID
FreeReceiveNbls(
    _In_ PNVNET_ADAPTER Adapter)
{
    PNET_BUFFER_LIST Nbl;
    PMDL Mdl;
    ULONG i;

    PAGED_CODE();

    for (i = 0; i < NVNET_RECEIVE_NBL_COUNT; ++i)
    {
        Nbl = Adapter->ReceiveNbls[i];
        if (!Nbl)
            continue;

        Mdl = NET_BUFFER_FIRST_MDL(NET_BUFFER_LIST_FIRST_NB(Nbl));
        if (Nbl->MiniportReserved[0])
            NdisFreeMemory(Nbl->MiniportReserved[0], 0, 0);
        NdisFreeNetBufferList(Nbl);
        if (Mdl)
            NdisFreeMdl(Mdl);
        Adapter->ReceiveNbls[i] = NULL;
    }
    Adapter->FreeReceiveNbls = NULL;

    if (Adapter->ReceiveNblPool)
    {
        NdisFreeNetBufferListPool(Adapter->ReceiveNblPool);
        Adapter->ReceiveNblPool = NULL;
    }
}

static
CODE_SEG("PAGE")
NDIS_STATUS
AllocateAdapterMemory(
    _In_ PNVNET_ADAPTER Adapter)
{
    NDIS_STATUS Status;

    PAGED_CODE();

    Status = AllocateTransmitBuffers(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    Status = AllocateTransmitBlocks(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    Status = AllocateTransmitDescriptors(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    Status = AllocateReceiveDescriptors(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    Status = AllocateReceiveBuffers(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    Status = AllocateReceiveNbls(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    NdisAllocateSpinLock(&Adapter->Send.Lock);
    NdisAllocateSpinLock(&Adapter->Receive.Lock);
    NdisAllocateSpinLock(&Adapter->Lock);

    return NDIS_STATUS_SUCCESS;
}

CODE_SEG("PAGE")
VOID
NvNetInitTransmitMemory(
    _In_ PNVNET_ADAPTER Adapter)
{
    PAGED_CODE();

    Adapter->Send.TcbSlots = NVNET_TRANSMIT_BLOCKS;
    Adapter->Send.TbdSlots = NVNET_TRANSMIT_DESCRIPTORS;

    if (Adapter->Features & DEV_HAS_HIGH_DMA)
    {
        NdisZeroMemory(Adapter->Send.HeadTbd.x64,
                       sizeof(NVNET_DESCRIPTOR_64) * NVNET_TRANSMIT_DESCRIPTORS);
    }
    else
    {
        NdisZeroMemory(Adapter->Send.HeadTbd.x32,
                       sizeof(NVNET_DESCRIPTOR_32) * NVNET_TRANSMIT_DESCRIPTORS);
    }
}

static
CODE_SEG("PAGE")
VOID
NvNetInitReceiveMemory(
    _In_ PNVNET_ADAPTER Adapter)
{
    NV_RBD NvRbd;
    ULONG i;

    PAGED_CODE();

    Adapter->CurrentRx = 0;

    if (Adapter->Features & DEV_HAS_HIGH_DMA)
    {
        for (i = 0; i < NVNET_RECEIVE_DESCRIPTORS; ++i)
        {
            NvRbd.x64 = &Adapter->Receive.NvRbd.x64[i];

            NvRbd.x64->AddressHigh = NdisGetPhysicalAddressHigh(Adapter->ReceiveBufferPhys)
                                     + i * NVNET_RECEIVE_BUFFER_SIZE;
            NvRbd.x64->AddressLow = NdisGetPhysicalAddressLow(Adapter->ReceiveBufferPhys)
                                    + i * NVNET_RECEIVE_BUFFER_SIZE;
            NvRbd.x64->VlanTag = 0;
            NvRbd.x64->FlagsLength = NV_RX2_AVAIL | NVNET_RECEIVE_BUFFER_SIZE;
        }
    }
    else
    {
        for (i = 0; i < NVNET_RECEIVE_DESCRIPTORS; ++i)
        {
            NvRbd.x32 = &Adapter->Receive.NvRbd.x32[i];

            NvRbd.x32->Address = NdisGetPhysicalAddressLow(Adapter->ReceiveBufferPhys)
                                 + i * NVNET_RECEIVE_BUFFER_SIZE;
            NvRbd.x32->FlagsLength = NV_RX_AVAIL | NVNET_RECEIVE_BUFFER_SIZE;
        }
    }
}


CODE_SEG("PAGE")
VOID
NvNetFreeAdapter(
    _In_ PNVNET_ADAPTER Adapter)
{
    ULONG i;
    ULONG DescriptorSize;

    PAGED_CODE();

    NDIS_DbgPrint(MIN_TRACE, ("()\n"));

    for (i = 0; i < RTL_NUMBER_OF(Adapter->WakeFrames); ++i)
    {
        PNVNET_WAKE_FRAME WakeFrame = Adapter->WakeFrames[i];

        if (!WakeFrame)
            continue;

        NdisFreeMemory(WakeFrame, sizeof(*WakeFrame), 0);
    }

    if (Adapter->InterruptHandle)
    {
        NdisMDeregisterInterruptEx(Adapter->InterruptHandle);
        Adapter->InterruptHandle = NULL;
    }

    if (Adapter->MediaDetectionTimer)
    {
        NdisCancelTimerObject(Adapter->MediaDetectionTimer);
        KeFlushQueuedDpcs();
        NdisFreeTimerObject(Adapter->MediaDetectionTimer);
        Adapter->MediaDetectionTimer = NULL;
    }

    if (Adapter->Features & DEV_HAS_HIGH_DMA)
    {
        DescriptorSize = sizeof(NVNET_DESCRIPTOR_64);
    }
    else
    {
        DescriptorSize = sizeof(NVNET_DESCRIPTOR_32);
    }
    if (Adapter->TbdOriginal)
    {
        NdisMFreeSharedMemory(Adapter->AdapterHandle,
                              DescriptorSize * NVNET_TRANSMIT_DESCRIPTORS,
                              FALSE,
                              Adapter->TbdOriginal,
                              Adapter->TbdPhysOriginal);
        Adapter->TbdOriginal = NULL;
    }
    if (Adapter->RbdOriginal)
    {
        NdisMFreeSharedMemory(Adapter->AdapterHandle,
                              DescriptorSize * NVNET_RECEIVE_DESCRIPTORS,
                              FALSE,
                              Adapter->RbdOriginal,
                              Adapter->RbdPhysOriginal);
        Adapter->RbdOriginal = NULL;
    }
    if (Adapter->SendBuffer)
    {
        ULONG Length = ALIGN_UP_BY(Adapter->MaximumFrameSize, NVNET_ALIGNMENT);

        for (i = 0; i < NVNET_TRANSMIT_BUFFERS; ++i)
        {
            PNVNET_TX_BUFFER_DATA SendBufferData = &Adapter->SendBufferAllocationData[i];

            if (!SendBufferData->VirtualAddress)
                continue;

            NdisMFreeSharedMemory(Adapter->AdapterHandle,
                                  Length,
                                  TRUE,
                                  SendBufferData->VirtualAddress,
                                  SendBufferData->PhysicalAddress);
        }

        NdisFreeMemory(Adapter->SendBuffer, NVNET_TRANSMIT_BUFFERS * sizeof(NVNET_TX_BUFFER), 0);
        Adapter->SendBuffer = NULL;
    }

    if (Adapter->ReceiveBuffer)
    {
        NdisMFreeSharedMemory(Adapter->AdapterHandle,
                              NVNET_RECEIVE_BUFFER_SIZE * NVNET_RECEIVE_DESCRIPTORS,
                              FALSE,
                              Adapter->ReceiveBuffer,
                              Adapter->ReceiveBufferPhys);
        Adapter->ReceiveBuffer = NULL;
    }

    FreeReceiveNbls(Adapter);

    if (Adapter->DmaHandle)
    {
        NdisMDeregisterScatterGatherDma(Adapter->DmaHandle);
        Adapter->DmaHandle = NULL;
    }

    if (Adapter->IoBase)
    {
        NdisMUnmapIoSpace(Adapter->AdapterHandle,
                          Adapter->IoBase,
                          Adapter->IoLength);
        Adapter->IoBase = NULL;
    }

    if (Adapter->ResetWorkItem)
        NdisFreeIoWorkItem(Adapter->ResetWorkItem);
    if (Adapter->PowerWorkItem)
        NdisFreeIoWorkItem(Adapter->PowerWorkItem);

    if (Adapter->Lock.SpinLock)
        NdisFreeSpinLock(&Adapter->Lock);
    if (Adapter->Send.Lock.SpinLock)
        NdisFreeSpinLock(&Adapter->Send.Lock);
    if (Adapter->Receive.Lock.SpinLock)
        NdisFreeSpinLock(&Adapter->Receive.Lock);

    NdisFreeMemory(Adapter->AdapterOriginal, sizeof(NVNET_ADAPTER), 0);
}

CODE_SEG("PAGE")
NDIS_STATUS
NTAPI
MiniportInitializeEx(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters)
{
    ULONG Size;
    PVOID UnalignedAdapter;
    PNVNET_ADAPTER Adapter;
    NDIS_STATUS Status;
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES Registration;
    NDIS_SG_DMA_DESCRIPTION DmaDescription;
    NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS InterruptCharacteristics;

    UNREFERENCED_PARAMETER(MiniportDriverContext);

    PAGED_CODE();

    NDIS_DbgPrint(MIN_TRACE, ("()\n"));

    Size = sizeof(NVNET_ADAPTER) + NdisGetSharedDataAlignment();

    Status = NdisAllocateMemoryWithTag((PVOID*)&UnalignedAdapter,
                                       Size,
                                       NVNET_TAG);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MAX_TRACE, ("Failed to allocate adapter\n"));
        return NDIS_STATUS_RESOURCES;
    }

    NdisZeroMemory(UnalignedAdapter, Size);
    Adapter = ALIGN_UP_POINTER_BY(UnalignedAdapter, NdisGetSharedDataAlignment());
    Adapter->AdapterOriginal = UnalignedAdapter;
    Adapter->AdapterHandle = MiniportAdapterHandle;
    Adapter->Paused = TRUE;

    NdisZeroMemory(&Registration, sizeof(Registration));
    Registration.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    Registration.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    Registration.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    Registration.MiniportAdapterContext = Adapter;
    Registration.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_HARDWARE_DEVICE |
                                  NDIS_MINIPORT_ATTRIBUTES_BUS_MASTER;
    Registration.CheckForHangTimeInSeconds = 2;
    Registration.InterfaceType = NdisInterfacePci;

    Status = NdisMSetMiniportAttributes(MiniportAdapterHandle,
                                        (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&Registration);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        goto Failure;
    }

    Adapter->ResetWorkItem = NdisAllocateIoWorkItem(MiniportAdapterHandle);
    Adapter->PowerWorkItem = NdisAllocateIoWorkItem(MiniportAdapterHandle);
    if (!Adapter->ResetWorkItem || !Adapter->PowerWorkItem)
    {
        Status = NDIS_STATUS_RESOURCES;
        goto Failure;
    }

    Status = NvNetReadConfiguration(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        goto Failure;
    }

    Status = NvNetRecognizeHardware(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        if (Status == NDIS_STATUS_ADAPTER_NOT_FOUND)
        {
            NvNetLogError(Adapter, NDIS_ERROR_CODE_ADAPTER_NOT_FOUND);
        }
        else if (Status == NDIS_STATUS_NOT_RECOGNIZED)
        {
            NvNetLogError(Adapter, NDIS_ERROR_CODE_UNSUPPORTED_CONFIGURATION);
        }

        goto Failure;
    }

    Status = NvNetInitializeAdapterResources(Adapter, MiniportInitParameters->AllocatedResources);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        goto Failure;
    }

    NdisZeroMemory(&DmaDescription, sizeof(DmaDescription));
    DmaDescription.Header.Type = NDIS_OBJECT_TYPE_SG_DMA_DESCRIPTION;
    DmaDescription.Header.Revision = NDIS_SG_DMA_DESCRIPTION_REVISION_1;
    DmaDescription.Header.Size = NDIS_SIZEOF_SG_DMA_DESCRIPTION_REVISION_1;
    if (Adapter->Features & DEV_HAS_HIGH_DMA)
        DmaDescription.Flags = NDIS_SG_DMA_64_BIT_ADDRESS;
    DmaDescription.MaximumPhysicalMapping = Adapter->MaximumFrameSize;

    Status = NdisMRegisterScatterGatherDma(Adapter->AdapterHandle,
                                           &DmaDescription,
                                           &Adapter->DmaHandle);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        Adapter->DmaHandle = NULL;
        NvNetLogError(Adapter, NDIS_ERROR_CODE_OUT_OF_RESOURCES);
        goto Failure;
    }

    Status = AllocateAdapterMemory(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MAX_TRACE, ("Failed to allocate adapter memory\n"));

        NvNetLogError(Adapter, NDIS_ERROR_CODE_OUT_OF_RESOURCES);
        goto Failure;
    }

    NvNetInitTransmitMemory(Adapter);
    NvNetInitReceiveMemory(Adapter);

    if (Adapter->Features & DEV_HAS_HIGH_DMA)
    {
        Adapter->TransmitPacket = NvNetTransmitPacket64;
        Adapter->ProcessTransmit = ProcessTransmitDescriptors64;
    }
    else
    {
        Adapter->TransmitPacket = NvNetTransmitPacket32;

        if (Adapter->Features & DEV_HAS_LARGEDESC)
        {
            Adapter->ProcessTransmit = ProcessTransmitDescriptors32;
        }
        else
        {
            Adapter->ProcessTransmit = ProcessTransmitDescriptorsLegacy;
        }
    }

    Status = NvNetGetPermanentMacAddress(Adapter, Adapter->PermanentMacAddress);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NvNetLogError(Adapter, NDIS_ERROR_CODE_NETWORK_ADDRESS);
        goto Failure;
    }

    if (!(Adapter->Flags & NV_USE_SOFT_MAC_ADDRESS))
    {
        ETH_COPY_NETWORK_ADDRESS(Adapter->CurrentMacAddress,
                                 Adapter->PermanentMacAddress);
    }

    NvNetSetupMacAddress(Adapter, Adapter->CurrentMacAddress);

    Status = NvNetInitNIC(Adapter, TRUE);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MAX_TRACE, ("Failed to initialize the NIC\n"));

        NvNetLogError(Adapter, NDIS_ERROR_CODE_HARDWARE_FAILURE);
        goto Failure;
    }

    NvNetDisableInterrupts(Adapter);
    NV_WRITE(Adapter, NvRegMIIStatus, NVREG_MIISTAT_MASK_ALL);
    NV_WRITE(Adapter, NvRegIrqStatus, NVREG_IRQSTAT_MASK);

    Status = NvNetSetGeneralAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        goto Failure;
    }

    NdisZeroMemory(&InterruptCharacteristics, sizeof(InterruptCharacteristics));
    InterruptCharacteristics.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_INTERRUPT;
    InterruptCharacteristics.Header.Revision = NDIS_MINIPORT_INTERRUPT_REVISION_1;
    InterruptCharacteristics.Header.Size = NDIS_SIZEOF_MINIPORT_INTERRUPT_CHARACTERISTICS_REVISION_1;
    InterruptCharacteristics.InterruptHandler = MiniportISR;
    InterruptCharacteristics.InterruptDpcHandler = MiniportInterruptDpc;
    InterruptCharacteristics.DisableInterruptHandler = MiniportDisableInterrupt;
    InterruptCharacteristics.EnableInterruptHandler = MiniportEnableInterrupt;

    Status = NdisMRegisterInterruptEx(Adapter->AdapterHandle,
                                      Adapter,
                                      &InterruptCharacteristics,
                                      &Adapter->InterruptHandle);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        Adapter->InterruptHandle = NULL;
        NvNetLogError(Adapter, NDIS_ERROR_CODE_INTERRUPT_CONNECT);
        goto Failure;
    }

    NvNetStartAdapter(Adapter);

    return NDIS_STATUS_SUCCESS;

Failure:
    NvNetFreeAdapter(Adapter);

    return Status;
}
