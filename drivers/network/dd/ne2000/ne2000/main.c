/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS Novell Eagle 2000 driver
 * FILE:        ne2000/main.c
 * PURPOSE:     Driver entry point
 * PROGRAMMERS: Casper S. Hornstrup (chorns@users.sourceforge.net)
 * REVISIONS:
 *   CSH 27/08-2000 Created
 */

#include <ne2000.h>

NTSTATUS
NTAPI
DriverEntry(
    PDRIVER_OBJECT DriverObject,
    PUNICODE_STRING RegistryPath);

static MINIPORT_ISR MiniportISR;


#if DBG

/* See debug.h for debug/trace constants */
ULONG DebugTraceLevel = 0;

#endif /* DBG */


/* List of supported OIDs */
static NDIS_OID MiniportOIDList[] = {
    OID_GEN_SUPPORTED_LIST,
    OID_GEN_HARDWARE_STATUS,
    OID_GEN_MEDIA_SUPPORTED,
    OID_GEN_MEDIA_IN_USE,
    OID_GEN_MAXIMUM_LOOKAHEAD,
    OID_GEN_MAXIMUM_FRAME_SIZE,
    OID_GEN_LINK_SPEED,
    OID_GEN_TRANSMIT_BUFFER_SPACE,
    OID_GEN_RECEIVE_BUFFER_SPACE,
    OID_GEN_TRANSMIT_BLOCK_SIZE,
    OID_GEN_RECEIVE_BLOCK_SIZE,
    OID_GEN_VENDOR_ID,
    OID_GEN_VENDOR_DESCRIPTION,
    OID_GEN_VENDOR_DRIVER_VERSION,
    OID_GEN_CURRENT_PACKET_FILTER,
    OID_GEN_CURRENT_LOOKAHEAD,
    OID_GEN_DRIVER_VERSION,
    OID_GEN_MAXIMUM_TOTAL_SIZE,
    OID_GEN_PROTOCOL_OPTIONS,
    OID_GEN_MAC_OPTIONS,
    OID_GEN_MEDIA_CONNECT_STATUS,
    OID_GEN_MAXIMUM_SEND_PACKETS,
    OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS,
    OID_802_3_MULTICAST_LIST,
    OID_802_3_MAXIMUM_LIST_SIZE,
    OID_802_3_MAC_OPTIONS,
    OID_GEN_PHYSICAL_MEDIUM,
    OID_GEN_STATISTICS,
    OID_GEN_INTERRUPT_MODERATION
};

DRIVER_INFORMATION      DriverInfo = { NULL, { NULL, NULL } };


static VOID NTAPI MiniportDisableInterrupt(
    IN  NDIS_HANDLE MiniportAdapterContext)
/*
 * FUNCTION: Disables interrupts from an adapter
 * ARGUMENTS:
 *     MiniportAdapterContext = Pointer to adapter context area
 */
{
    NDIS_DbgPrint(MAX_TRACE, ("Called. (MiniportDisableInterrupt).\n"));
#ifndef NOCARD
    NICDisableInterrupts((PNIC_ADAPTER)MiniportAdapterContext);
#endif
}


static VOID NTAPI MiniportEnableInterrupt(
    IN  NDIS_HANDLE MiniportAdapterContext)
/*
 * FUNCTION: Enables interrupts from an adapter
 * ARGUMENTS:
 *     MiniportAdapterContext = Pointer to adapter context area
 */
{
    NDIS_DbgPrint(MAX_TRACE, ("Called. (MiniportEnableInterrupt).\n"));
#ifndef NOCARD
    NICEnableInterrupts((PNIC_ADAPTER)MiniportAdapterContext);
#endif
}


static VOID MiFreeReceiveNbls(
    IN  PNIC_ADAPTER Adapter)
{
    PNET_BUFFER_LIST Nbl;
    PMDL Mdl;
    ULONG i;

    for (i = 0; i < DRIVER_RECEIVE_NBL_COUNT; i++) {
        Nbl = Adapter->ReceiveNbls[i];
        if (Nbl == NULL)
            continue;

        Mdl = NET_BUFFER_FIRST_MDL(NET_BUFFER_LIST_FIRST_NB(Nbl));
        if (Nbl->MiniportReserved[0] != NULL)
            NdisFreeMemory(Nbl->MiniportReserved[0], 0, 0);
        NdisFreeNetBufferList(Nbl);
        if (Mdl != NULL)
            NdisFreeMdl(Mdl);
        Adapter->ReceiveNbls[i] = NULL;
    }
    Adapter->FreeReceiveNbls = NULL;

    if (Adapter->ReceiveNblPool != NULL) {
        NdisFreeNetBufferListPool(Adapter->ReceiveNblPool);
        Adapter->ReceiveNblPool = NULL;
    }
}


static NDIS_STATUS MiAllocateReceiveNbls(
    IN  PNIC_ADAPTER Adapter)
{
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParameters;
    PNET_BUFFER_LIST Nbl;
    PUCHAR Buffer;
    PMDL Mdl;
    ULONG i;

    NdisZeroMemory(&PoolParameters, sizeof(PoolParameters));
    PoolParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParameters.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.Header.Size = NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    PoolParameters.fAllocateNetBuffer = TRUE;
    PoolParameters.PoolTag = DRIVER_TAG;

    Adapter->ReceiveNblPool = NdisAllocateNetBufferListPool(Adapter->MiniportAdapterHandle, &PoolParameters);
    if (Adapter->ReceiveNblPool == NULL)
        return NDIS_STATUS_RESOURCES;

    for (i = 0; i < DRIVER_RECEIVE_NBL_COUNT; i++) {
        Buffer = NdisAllocateMemoryWithTagPriority(Adapter->MiniportAdapterHandle,
                                                   DRIVER_FRAME_SIZE,
                                                   DRIVER_TAG,
                                                   NormalPoolPriority);
        if (Buffer == NULL)
            return NDIS_STATUS_RESOURCES;

        Mdl = NdisAllocateMdl(Adapter->MiniportAdapterHandle, Buffer, DRIVER_FRAME_SIZE);
        if (Mdl == NULL) {
            NdisFreeMemory(Buffer, 0, 0);
            return NDIS_STATUS_RESOURCES;
        }

        Nbl = NdisAllocateNetBufferAndNetBufferList(Adapter->ReceiveNblPool, 0, 0, Mdl, 0, DRIVER_FRAME_SIZE);
        if (Nbl == NULL) {
            NdisFreeMdl(Mdl);
            NdisFreeMemory(Buffer, 0, 0);
            return NDIS_STATUS_RESOURCES;
        }

        Nbl->MiniportReserved[0] = Buffer;
        Nbl->SourceHandle = Adapter->MiniportAdapterHandle;
        Adapter->ReceiveNbls[i] = Nbl;
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = Adapter->FreeReceiveNbls;
        Adapter->FreeReceiveNbls = Nbl;
    }

    return NDIS_STATUS_SUCCESS;
}


static VOID NTAPI MiniportHaltEx(
    IN  NDIS_HANDLE      MiniportAdapterContext,
    IN  NDIS_HALT_ACTION HaltAction)
/*
 * FUNCTION: Deallocates resources for and halts an adapter
 * ARGUMENTS:
 *     MiniportAdapterContext = Pointer to adapter context area
 */
{
    PNIC_ADAPTER Adapter = (PNIC_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(HaltAction);

    ASSERT_IRQL_EQUAL(PASSIVE_LEVEL);

    NDIS_DbgPrint(MAX_TRACE, ("Called.\n"));
#ifndef NOCARD
    /* Stop the NIC */
    if (Adapter->IOPortRangeRegistered)
        NICStop(Adapter);
#endif

    if (Adapter->InterruptHandle)
        /* Deregister interrupt */
        NdisMDeregisterInterruptEx(Adapter->InterruptHandle);

    if (Adapter->IOPortRangeRegistered)
        /* Deregister I/O port range */
        NdisMDeregisterIoPortRange(
            Adapter->MiniportAdapterHandle,
            Adapter->IoBaseAddress,
            0x20,
            Adapter->IOBase);

    /* Remove adapter from global adapter list */
    if ((&Adapter->ListEntry)->Blink != NULL) {
        RemoveEntryList(&Adapter->ListEntry);
    }

    MiFreeReceiveNbls(Adapter);
    NdisFreeSpinLock(&Adapter->Lock);

    /* Free adapter context area */
    NdisFreeMemory(Adapter, sizeof(NIC_ADAPTER), 0);
}


static NDIS_STATUS MiQueryResources(
    IN  PNIC_ADAPTER                   Adapter,
    IN  PNDIS_MINIPORT_INIT_PARAMETERS InitParameters)
{
    PCM_PARTIAL_RESOURCE_LIST AssignedResources = InitParameters->AllocatedResources;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    UINT i;

    if (AssignedResources == NULL)
        return NDIS_STATUS_RESOURCES;

    for (i = 0; i < AssignedResources->Count; i++)
    {
        Descriptor = AssignedResources->PartialDescriptors + i;
        if (Descriptor->Type == CmResourceTypePort && Adapter->IoBaseAddress == 0)
            Adapter->IoBaseAddress = Descriptor->u.Port.Start.LowPart;
    }

    return Adapter->IoBaseAddress ? NDIS_STATUS_SUCCESS : NDIS_STATUS_RESOURCES;
}

static VOID NTAPI MiniportShutdownEx(
    IN  NDIS_HANDLE          MiniportAdapterContext,
    IN  NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    UNREFERENCED_PARAMETER(ShutdownAction);
  #ifndef NOCARD
    NICStop((PNIC_ADAPTER)MiniportAdapterContext);
  #endif
}

static NDIS_STATUS MiSetRegistrationAttributes(
    IN  PNIC_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES Registration;

    NdisZeroMemory(&Registration, sizeof(Registration));
    Registration.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    Registration.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    Registration.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    Registration.MiniportAdapterContext = Adapter;
    Registration.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_HARDWARE_DEVICE;
    Registration.InterfaceType = NdisInterfaceIsa;

    return NdisMSetMiniportAttributes(Adapter->MiniportAdapterHandle,
                                      (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&Registration);
}


static NDIS_STATUS MiSetGeneralAttributes(
    IN  PNIC_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES General;

    NdisZeroMemory(&General, sizeof(General));
    General.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    General.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    General.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    General.MediaType = NdisMedium802_3;
    General.PhysicalMediumType = NdisPhysicalMedium802_3;
    General.MtuSize = DRIVER_FRAME_SIZE - DRIVER_HEADER_SIZE;
    General.MaxXmitLinkSpeed = 10000000;
    General.MaxRcvLinkSpeed = 10000000;
    General.XmitLinkSpeed = 10000000;
    General.RcvLinkSpeed = 10000000;
    General.MediaConnectState = MediaConnectStateConnected;
    General.MediaDuplexState = MediaDuplexStateUnknown;
    General.LookaheadSize = DRIVER_FRAME_SIZE - DRIVER_HEADER_SIZE;
    General.MacOptions = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                         NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                         NDIS_MAC_OPTION_NO_LOOPBACK;
    General.SupportedPacketFilters = NDIS_PACKET_TYPE_DIRECTED |
                                     NDIS_PACKET_TYPE_MULTICAST |
                                     NDIS_PACKET_TYPE_ALL_MULTICAST |
                                     NDIS_PACKET_TYPE_BROADCAST |
                                     NDIS_PACKET_TYPE_PROMISCUOUS;
    General.MaxMulticastListSize = Adapter->MaxMulticastListSize;
    General.MacAddressLength = DRIVER_LENGTH_OF_ADDRESS;
    NdisMoveMemory(General.PermanentMacAddress, Adapter->PermanentAddress, DRIVER_LENGTH_OF_ADDRESS);
    NdisMoveMemory(General.CurrentMacAddress, Adapter->StationAddress, DRIVER_LENGTH_OF_ADDRESS);
    General.AccessType = NET_IF_ACCESS_BROADCAST;
    General.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    General.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    General.IfType = IF_TYPE_ETHERNET_CSMACD;
    General.IfConnectorPresent = TRUE;
    General.SupportedStatistics = NDIS_STATISTICS_XMIT_OK_SUPPORTED |
                                  NDIS_STATISTICS_RCV_OK_SUPPORTED |
                                  NDIS_STATISTICS_XMIT_ERROR_SUPPORTED |
                                  NDIS_STATISTICS_RCV_ERROR_SUPPORTED |
                                  NDIS_STATISTICS_RCV_NO_BUFFER_SUPPORTED |
                                  NDIS_STATISTICS_RCV_CRC_ERROR_SUPPORTED;
    General.SupportedPauseFunctions = NdisPauseFunctionsUnsupported;
    General.SupportedOidList = MiniportOIDList;
    General.SupportedOidListLength = sizeof(MiniportOIDList);

    return NdisMSetMiniportAttributes(Adapter->MiniportAdapterHandle,
                                      (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&General);
}


static NDIS_STATUS NTAPI MiniportInitializeEx(
    IN  NDIS_HANDLE                    MiniportAdapterHandle,
    IN  NDIS_HANDLE                    MiniportDriverContext,
    IN  PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters)
{
    UINT i;
    NDIS_STATUS Status;
    PNIC_ADAPTER Adapter;
    NDIS_HANDLE ConfigurationHandle;
    NDIS_CONFIGURATION_OBJECT ConfigurationObject;
    NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS InterruptCharacteristics;
    PUCHAR RegNetworkAddress = NULL;
    UINT RegNetworkAddressLength = 0;

    UNREFERENCED_PARAMETER(MiniportDriverContext);

    ASSERT_IRQL_EQUAL(PASSIVE_LEVEL);

    NDIS_DbgPrint(MAX_TRACE, ("Called (Adapter %X).\n", MiniportAdapterHandle));

    Adapter = NdisAllocateMemoryWithTagPriority(MiniportAdapterHandle,
                                                sizeof(NIC_ADAPTER),
                                                DRIVER_TAG,
                                                NormalPoolPriority);
    if (Adapter == NULL) {
        NDIS_DbgPrint(MIN_TRACE, ("Insufficient resources.\n"));
        return NDIS_STATUS_RESOURCES;
    }

    NdisZeroMemory(Adapter, sizeof(NIC_ADAPTER));
    Adapter->MiniportAdapterHandle  = MiniportAdapterHandle;
    Adapter->MaxMulticastListSize   = DRIVER_MAX_MULTICAST_LIST_SIZE;
    Adapter->InterruptMask          = DRIVER_INTERRUPT_MASK;
    Adapter->LookaheadSize          = DRIVER_MAXIMUM_LOOKAHEAD;
    Adapter->Paused                 = TRUE;
    NdisAllocateSpinLock(&Adapter->Lock);

    Status = MiSetRegistrationAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Failure;

    Status = MiQueryResources(Adapter, MiniportInitParameters);
    if (Status != NDIS_STATUS_SUCCESS) {
        NDIS_DbgPrint(MIN_TRACE, ("No I/O port range assigned.\n"));
        goto Failure;
    }

    Status = NdisMRegisterIoPortRange(
        (PVOID*)&Adapter->IOBase,
        MiniportAdapterHandle,
        Adapter->IoBaseAddress,
        0x20);

    if (Status != NDIS_STATUS_SUCCESS) {
        NDIS_DbgPrint(MIN_TRACE, ("Cannot register port range. Status (0x%X).\n", Status));
        goto Failure;
    }

    Adapter->IOPortRangeRegistered = TRUE;

    if (!NICCheck(Adapter)) {
        NDIS_DbgPrint(MID_TRACE, ("No adapter found at (0x%X).\n", Adapter->IoBaseAddress));
        Status = NDIS_STATUS_ADAPTER_NOT_FOUND;
        goto Failure;
    } else
        NDIS_DbgPrint(MID_TRACE, ("Adapter found at (0x%X).\n", Adapter->IoBaseAddress));

    /* Initialize NIC */
#ifndef NOCARD
    Status = NICInitialize(Adapter);
    if (Status != NDIS_STATUS_SUCCESS) {
        NDIS_DbgPrint(MIN_TRACE,("No NE2000 or compatible network adapter found at address 0x%X.\n",
            Adapter->IOBase));

        NDIS_DbgPrint(MID_TRACE, ("Status (0x%X).\n", Status));
        goto Failure;
    }

    for (i = 0; i < DRIVER_LENGTH_OF_ADDRESS; i++)
        Adapter->StationAddress[i] = Adapter->PermanentAddress[i];

    ConfigurationObject.Header.Type = NDIS_OBJECT_TYPE_CONFIGURATION_OBJECT;
    ConfigurationObject.Header.Revision = NDIS_CONFIGURATION_OBJECT_REVISION_1;
    ConfigurationObject.Header.Size = NDIS_SIZEOF_CONFIGURATION_OBJECT_REVISION_1;
    ConfigurationObject.NdisHandle = MiniportAdapterHandle;
    ConfigurationObject.Flags = 0;
    if (NdisOpenConfigurationEx(&ConfigurationObject, &ConfigurationHandle) == NDIS_STATUS_SUCCESS)
    {
         NdisReadNetworkAddress(&Status, (PVOID *)&RegNetworkAddress, &RegNetworkAddressLength, ConfigurationHandle);
         if(Status == NDIS_STATUS_SUCCESS && RegNetworkAddressLength == DRIVER_LENGTH_OF_ADDRESS &&
            !(RegNetworkAddress[0] & 0x01))
         {
             NDIS_DbgPrint(MID_TRACE,("NdisReadNetworkAddress returned successfully, address %x:%x:%x:%x:%x:%x\n",
                     RegNetworkAddress[0], RegNetworkAddress[1], RegNetworkAddress[2], RegNetworkAddress[3],
                     RegNetworkAddress[4], RegNetworkAddress[5]));
             NdisMoveMemory(Adapter->StationAddress, RegNetworkAddress, DRIVER_LENGTH_OF_ADDRESS);
         }

         NdisCloseConfiguration(ConfigurationHandle);
    }

    NDIS_DbgPrint(MID_TRACE, ("BOARDDATA:\n"));
    for (i = 0; i < 4; i++) {
        NDIS_DbgPrint(MID_TRACE, ("%02X %02X %02X %02X\n",
            Adapter->SAPROM[i*4+0],
            Adapter->SAPROM[i*4+1],
            Adapter->SAPROM[i*4+2],
            Adapter->SAPROM[i*4+3]));
    }

    /* Setup adapter structure */
    Adapter->TXStart   = ((ULONG_PTR)Adapter->RamBase >> 8);
    Adapter->TXCount   = DRIVER_DEFAULT_TX_BUFFER_COUNT;
    Adapter->TXFree    = DRIVER_DEFAULT_TX_BUFFER_COUNT;
    Adapter->TXCurrent = -1;
    Adapter->PageStart = Adapter->TXStart + Adapter->TXCount;
    Adapter->PageStop  = Adapter->TXStart + (Adapter->RamSize >> 8);

    /* Initialize multicast address mask to accept all */
    for (i = 0; i < 8; i++)
        Adapter->MulticastAddressMask[i] = 0xFF;

    /* Setup the NIC */
    NICSetup(Adapter);

    NDIS_DbgPrint(MID_TRACE, ("TXStart (0x%X)  TXCount (0x%X)  PageStart (0x%X)\n",
        Adapter->TXStart,
        Adapter->TXCount,
        Adapter->PageStart));

    NDIS_DbgPrint(MID_TRACE, ("PageStop (0x%X)  CurrentPage (0x%X)  NextPacket (0x%X).\n",
        Adapter->PageStop,
        Adapter->CurrentPage,
        Adapter->NextPacket));
#endif
    Status = MiAllocateReceiveNbls(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Failure;

    Status = MiSetGeneralAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Failure;

    NdisZeroMemory(&InterruptCharacteristics, sizeof(InterruptCharacteristics));
    InterruptCharacteristics.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_INTERRUPT;
    InterruptCharacteristics.Header.Revision = NDIS_MINIPORT_INTERRUPT_REVISION_1;
    InterruptCharacteristics.Header.Size = NDIS_SIZEOF_MINIPORT_INTERRUPT_CHARACTERISTICS_REVISION_1;
    InterruptCharacteristics.InterruptHandler = MiniportISR;
    InterruptCharacteristics.InterruptDpcHandler = MiniportInterruptDpc;
    InterruptCharacteristics.DisableInterruptHandler = MiniportDisableInterrupt;
    InterruptCharacteristics.EnableInterruptHandler = MiniportEnableInterrupt;
    Status = NdisMRegisterInterruptEx(MiniportAdapterHandle,
                                      Adapter,
                                      &InterruptCharacteristics,
                                      &Adapter->InterruptHandle);
    if (Status != NDIS_STATUS_SUCCESS) {
        NDIS_DbgPrint(MIN_TRACE, ("Cannot register interrupt. Status (0x%X).\n", Status));
        Adapter->InterruptHandle = NULL;
        goto Failure;
    }

#ifndef NOCARD
    /* Start the NIC */
    NICStart(Adapter);
#endif

    /* Add adapter to the global adapter list */
    InsertTailList(&DriverInfo.AdapterListHead, &Adapter->ListEntry);

    NDIS_DbgPrint(MAX_TRACE, ("Leaving.\n"));

    return NDIS_STATUS_SUCCESS;

Failure:
    MiniportHaltEx((NDIS_HANDLE)Adapter, NdisHaltDeviceInitializationFailed);
    return Status;
}


static BOOLEAN NTAPI MiniportISR(
    IN  NDIS_HANDLE MiniportInterruptContext,
    OUT PBOOLEAN    QueueDefaultInterruptDpc,
    OUT PULONG      TargetProcessors)
{
    PNIC_ADAPTER Adapter = (PNIC_ADAPTER)MiniportInterruptContext;
    UCHAR ISRValue;

    NDIS_DbgPrint(MAX_TRACE, ("Called.\n"));

    *TargetProcessors = 0;
    *QueueDefaultInterruptDpc = FALSE;

    if (Adapter->InterruptsDisabled)
        return FALSE;

    NdisRawReadPortUchar(Adapter->IOBase + PG0_ISR, &ISRValue);
    if ((ISRValue & Adapter->InterruptMask) == 0)
        return FALSE;

    NICDisableInterrupts(Adapter);
    Adapter->InterruptsDisabled = TRUE;

    *QueueDefaultInterruptDpc = TRUE;
    return TRUE;
}


static NDIS_STATUS NTAPI MiniportQueryInformation(
    IN  NDIS_HANDLE MiniportAdapterContext,
    IN  NDIS_OID    Oid,
    IN  PVOID       InformationBuffer,
    IN  ULONG       InformationBufferLength,
    OUT PULONG      BytesWritten,
    OUT PULONG      BytesNeeded)
/*
 * FUNCTION: Handler to process queries
 * ARGUMENTS:
 *     MiniportAdapterContext  = Pointer to adapter context area
 *     Oid                     = OID code designating query operation
 *     InformationBuffer       = Address of return buffer
 *     InformationBufferLength = Length of return buffer
 *     BytesWritten            = Address of buffer to place number of bytes returned
 *     BytesNeeded             = Address of buffer to place number of bytes needed
 *                               in InformationBuffer for specified OID
 * RETURNS:
 *     Status of operation
 */
{
    NDIS_STATUS Status;
    PVOID CopyFrom;
    UINT CopySize;
    ULONG GenericULONG;
    USHORT GenericUSHORT;
    NDIS_MEDIUM Medium   = NdisMedium802_3;
    NDIS_STATISTICS_INFO Statistics;
    NDIS_INTERRUPT_MODERATION_PARAMETERS Moderation;
    PNIC_ADAPTER Adapter = (PNIC_ADAPTER)MiniportAdapterContext;

    NDIS_DbgPrint(MAX_TRACE, ("Called. Oid (0x%X).\n", Oid));

    Status   = NDIS_STATUS_SUCCESS;
    CopyFrom = (PVOID)&GenericULONG;
    CopySize = sizeof(ULONG);

    switch (Oid) {
    case OID_GEN_SUPPORTED_LIST:
        CopyFrom = (PVOID)&MiniportOIDList;
        CopySize = sizeof(MiniportOIDList);
        break;
    case OID_GEN_STATISTICS:
        NdisZeroMemory(&Statistics, sizeof(Statistics));
        Statistics.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
        Statistics.Header.Revision = NDIS_STATISTICS_INFO_REVISION_1;
        Statistics.Header.Size = NDIS_SIZEOF_STATISTICS_INFO_REVISION_1;
        Statistics.SupportedStatistics = NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_RCV |
                                         NDIS_STATISTICS_FLAGS_VALID_RCV_ERROR |
                                         NDIS_STATISTICS_FLAGS_VALID_RCV_DISCARDS |
                                         NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_XMIT |
                                         NDIS_STATISTICS_FLAGS_VALID_XMIT_ERROR;
        Statistics.ifInErrors = Adapter->FrameAlignmentErrors + Adapter->CrcErrors;
        Statistics.ifInDiscards = Statistics.ifInErrors + Adapter->MissedPackets + Adapter->ReceiveNoBuffers;
        Statistics.ifHCInUcastPkts = Adapter->ReceiveOk;
        Statistics.ifHCOutUcastPkts = Adapter->TransmitOk;
        Statistics.ifOutErrors = Adapter->TransmitErrors;
        CopyFrom = (PVOID)&Statistics;
        CopySize = sizeof(Statistics);
        break;
    case OID_GEN_INTERRUPT_MODERATION:
        NdisZeroMemory(&Moderation, sizeof(Moderation));
        Moderation.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
        Moderation.Header.Revision = NDIS_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
        Moderation.Header.Size = NDIS_SIZEOF_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
        Moderation.InterruptModeration = NdisInterruptModerationNotSupported;
        CopyFrom = (PVOID)&Moderation;
        CopySize = sizeof(Moderation);
        break;
    case OID_GEN_HARDWARE_STATUS:
        GenericULONG = (ULONG)NdisHardwareStatusReady;
        break;
    case OID_GEN_MEDIA_SUPPORTED:
    case OID_GEN_MEDIA_IN_USE:
        CopyFrom = (PVOID)&Medium;
        CopySize = sizeof(NDIS_MEDIUM);
        break;
    case OID_GEN_MAXIMUM_LOOKAHEAD:
        GenericULONG = DRIVER_MAXIMUM_LOOKAHEAD;
        break;
    case OID_GEN_MAXIMUM_FRAME_SIZE:
        GenericULONG = DRIVER_FRAME_SIZE - DRIVER_HEADER_SIZE;
        break;
    case OID_GEN_LINK_SPEED:
        GenericULONG = 100000;  /* 10Mbps */
        break;
    case OID_GEN_TRANSMIT_BUFFER_SPACE:
        GenericULONG = Adapter->TXCount * DRIVER_BLOCK_SIZE;
        break;
    case OID_GEN_RECEIVE_BUFFER_SPACE:
        GenericULONG = Adapter->RamSize -
                       (ULONG_PTR)Adapter->RamBase -
                       (Adapter->TXCount * DRIVER_BLOCK_SIZE);
        break;
    case OID_GEN_TRANSMIT_BLOCK_SIZE:
        GenericULONG = DRIVER_BLOCK_SIZE;
        break;
    case OID_GEN_RECEIVE_BLOCK_SIZE:
        GenericULONG = DRIVER_BLOCK_SIZE;
        break;
    case OID_GEN_VENDOR_ID:
        NdisMoveMemory(&GenericULONG, &Adapter->PermanentAddress, 3);
        GenericULONG &= 0xFFFFFF00;
        GenericULONG |= 0x01;
        break;
    case OID_GEN_VENDOR_DESCRIPTION:
        CopyFrom = (PVOID)&DRIVER_VENDOR_DESCRIPTION;
        CopySize = sizeof(DRIVER_VENDOR_DESCRIPTION);
        break;
    case OID_GEN_VENDOR_DRIVER_VERSION:
        GenericUSHORT = (USHORT)DRIVER_VENDOR_DRIVER_VERSION;
        CopyFrom      = (PVOID)&GenericUSHORT;
        CopySize      = sizeof(USHORT);
        break;
    case OID_GEN_CURRENT_PACKET_FILTER:
        GenericULONG = Adapter->PacketFilter;
        break;
    case OID_GEN_CURRENT_LOOKAHEAD:
        GenericULONG = Adapter->LookaheadSize;
        break;
    case OID_GEN_DRIVER_VERSION:
        GenericUSHORT = ((USHORT)NDIS_MINIPORT_MAJOR_VERSION << 8) | NDIS_MINIPORT_MINOR_VERSION;
        CopyFrom      = (PVOID)&GenericUSHORT;
        CopySize      = sizeof(USHORT);
        break;
    case OID_GEN_MAXIMUM_TOTAL_SIZE:
        GenericULONG = DRIVER_FRAME_SIZE;
        break;
    case OID_GEN_PROTOCOL_OPTIONS:
        NDIS_DbgPrint(MID_TRACE, ("OID_GEN_PROTOCOL_OPTIONS.\n"));
        Status = NDIS_STATUS_NOT_SUPPORTED;
        break;
    case OID_GEN_MAC_OPTIONS:
        GenericULONG = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                       NDIS_MAC_OPTION_RECEIVE_SERIALIZED  |
                       NDIS_MAC_OPTION_TRANSFERS_NOT_PEND  |
                       NDIS_MAC_OPTION_NO_LOOPBACK;
        break;
    case OID_GEN_MEDIA_CONNECT_STATUS:
        GenericULONG = (ULONG)NdisMediaStateConnected;
        break;
    case OID_GEN_MAXIMUM_SEND_PACKETS:
        GenericULONG = 1;
        break;
    case OID_802_3_PERMANENT_ADDRESS:
        CopyFrom = (PVOID)&Adapter->PermanentAddress;
        CopySize = DRIVER_LENGTH_OF_ADDRESS;
        break;
    case OID_802_3_CURRENT_ADDRESS:
        CopyFrom = (PVOID)&Adapter->StationAddress;
        CopySize = DRIVER_LENGTH_OF_ADDRESS;
        break;
    case OID_802_3_MULTICAST_LIST:
        NDIS_DbgPrint(MID_TRACE, ("OID_802_3_MULTICAST_LIST.\n"));
        Status = NDIS_STATUS_NOT_SUPPORTED;
        break;
    case OID_802_3_MAXIMUM_LIST_SIZE:
        GenericULONG = Adapter->MaxMulticastListSize;
        break;
    case OID_802_3_MAC_OPTIONS:
        NDIS_DbgPrint(MID_TRACE, ("OID_802_3_MAC_OPTIONS.\n"));
        Status = NDIS_STATUS_NOT_SUPPORTED;
        break;
    case OID_GEN_PHYSICAL_MEDIUM:
        GenericULONG = NdisPhysicalMedium802_3;
        break;
    default:
        NDIS_DbgPrint(MIN_TRACE, ("Unknown OID (0x%X).\n", Oid));
        Status = NDIS_STATUS_NOT_SUPPORTED;
        break;
    }

    if (Status == NDIS_STATUS_SUCCESS) {
        if (CopySize > InformationBufferLength) {
            *BytesNeeded  = CopySize;
            *BytesWritten = 0;
            Status        = NDIS_STATUS_INVALID_LENGTH;
        } else {
            NdisMoveMemory(InformationBuffer, CopyFrom, CopySize);
            *BytesWritten = CopySize;
            *BytesNeeded  = 0;
         }
    }

    NDIS_DbgPrint(MAX_TRACE, ("Leaving. Status is (0x%X).\n", Status));

    return Status;
}


static PNET_BUFFER_LIST MiFailNblChain(
    IN  PNET_BUFFER_LIST NetBufferLists,
    IN  NDIS_STATUS      Status)
{
    PNET_BUFFER_LIST Nbl;

    for (Nbl = NetBufferLists; Nbl != NULL; Nbl = NET_BUFFER_LIST_NEXT_NBL(Nbl))
        NET_BUFFER_LIST_STATUS(Nbl) = Status;

    return NetBufferLists;
}


static VOID MiCompleteSends(
    IN  PNIC_ADAPTER     Adapter,
    IN  PNET_BUFFER_LIST NetBufferLists,
    IN  BOOLEAN          DispatchLevel)
{
    if (NetBufferLists != NULL)
        NdisMSendNetBufferListsComplete(Adapter->MiniportAdapterHandle,
                                        NetBufferLists,
                                        DispatchLevel ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);
}


static VOID NTAPI MiniportSendNetBufferLists(
    IN  NDIS_HANDLE      MiniportAdapterContext,
    IN  PNET_BUFFER_LIST NetBufferLists,
    IN  NDIS_PORT_NUMBER PortNumber,
    IN  ULONG            SendFlags)
{
    PNIC_ADAPTER Adapter = (PNIC_ADAPTER)MiniportAdapterContext;
    BOOLEAN DispatchLevel = NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags);
    PNET_BUFFER_LIST Nbl, Next, CompleteHead;

    UNREFERENCED_PARAMETER(PortNumber);

    NdisAcquireSpinLock(&Adapter->Lock);

    if (Adapter->Paused) {
        NdisReleaseSpinLock(&Adapter->Lock);
        MiCompleteSends(Adapter, MiFailNblChain(NetBufferLists, NDIS_STATUS_PAUSED), DispatchLevel);
        return;
    }

    NDIS_DbgPrint(MID_TRACE, ("Queueing packet.\n"));

    for (Nbl = NetBufferLists; Nbl != NULL; Nbl = Next) {
        Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;
        Nbl->MiniportReserved[0] = NET_BUFFER_LIST_FIRST_NB(Nbl);
        NICAppendNbl(&Adapter->TXQueueHead, &Adapter->TXQueueTail, Nbl);
    }

#ifndef NOCARD
    NICTransmit(Adapter);
#endif

    CompleteHead = Adapter->CompleteHead;
    Adapter->CompleteHead = NULL;
    Adapter->CompleteTail = NULL;

    NdisReleaseSpinLock(&Adapter->Lock);

    MiCompleteSends(Adapter, CompleteHead, DispatchLevel);
}


static VOID NTAPI MiniportCancelSend(
    IN  NDIS_HANDLE MiniportAdapterContext,
    IN  PVOID       CancelId)
{
    PNIC_ADAPTER Adapter = (PNIC_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST Nbl, Next;
    PNET_BUFFER_LIST KeepHead = NULL, KeepTail = NULL;
    PNET_BUFFER_LIST CompleteHead = NULL, CompleteTail = NULL;

    NdisAcquireSpinLock(&Adapter->Lock);

    Nbl = Adapter->TXQueueHead;
    if (Nbl != NULL && Nbl->MiniportReserved[0] != NET_BUFFER_LIST_FIRST_NB(Nbl)) {
        Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NICAppendNbl(&KeepHead, &KeepTail, Nbl);
        Nbl = Next;
    }

    for (; Nbl != NULL; Nbl = Next) {
        Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        if (NDIS_GET_NET_BUFFER_LIST_CANCEL_ID(Nbl) == CancelId) {
            NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SEND_ABORTED;
            NICAppendNbl(&CompleteHead, &CompleteTail, Nbl);
        } else {
            NICAppendNbl(&KeepHead, &KeepTail, Nbl);
        }
    }

    Adapter->TXQueueHead = KeepHead;
    Adapter->TXQueueTail = KeepTail;
    NdisReleaseSpinLock(&Adapter->Lock);

    MiCompleteSends(Adapter, CompleteHead, FALSE);
}


static VOID NTAPI MiniportReturnNetBufferLists(
    IN  NDIS_HANDLE      MiniportAdapterContext,
    IN  PNET_BUFFER_LIST NetBufferLists,
    IN  ULONG            ReturnFlags)
{
    PNIC_ADAPTER Adapter = (PNIC_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST Nbl, Next;
    BOOLEAN CompletePause = FALSE;

    UNREFERENCED_PARAMETER(ReturnFlags);

    NdisAcquireSpinLock(&Adapter->Lock);
    for (Nbl = NetBufferLists; Nbl != NULL; Nbl = Next) {
        Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = Adapter->FreeReceiveNbls;
        Adapter->FreeReceiveNbls = Nbl;
        Adapter->ReceiveNblsOutstanding--;
    }

    if (Adapter->PausePending && Adapter->ReceiveNblsOutstanding == 0) {
        Adapter->PausePending = FALSE;
        CompletePause = TRUE;
    }
    NdisReleaseSpinLock(&Adapter->Lock);

    if (CompletePause)
        NdisMPauseComplete(Adapter->MiniportAdapterHandle);
}


static NDIS_STATUS NTAPI MiniportPause(
    IN  NDIS_HANDLE                     MiniportAdapterContext,
    IN  PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters)
{
    PNIC_ADAPTER Adapter = (PNIC_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST Pending, Completed;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(PauseParameters);

    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->Paused = TRUE;
    Pending = Adapter->TXQueueHead;
    Adapter->TXQueueHead = NULL;
    Adapter->TXQueueTail = NULL;
    Completed = Adapter->CompleteHead;
    Adapter->CompleteHead = NULL;
    Adapter->CompleteTail = NULL;
    if (Adapter->ReceiveNblsOutstanding != 0) {
        Adapter->PausePending = TRUE;
        Status = NDIS_STATUS_PENDING;
    }
    NdisReleaseSpinLock(&Adapter->Lock);

    MiCompleteSends(Adapter, Completed, FALSE);
    MiCompleteSends(Adapter, MiFailNblChain(Pending, NDIS_STATUS_PAUSED), FALSE);
    return Status;
}


static NDIS_STATUS NTAPI MiniportRestart(
    IN  NDIS_HANDLE                       MiniportAdapterContext,
    IN  PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters)
{
    PNIC_ADAPTER Adapter = (PNIC_ADAPTER)MiniportAdapterContext;
    NDIS_STATUS_INDICATION Indication;
    NDIS_LINK_STATE LinkState;

    UNREFERENCED_PARAMETER(RestartParameters);

    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->Paused = FALSE;
    NdisReleaseSpinLock(&Adapter->Lock);

    NdisZeroMemory(&LinkState, sizeof(LinkState));
    LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
    LinkState.Header.Size = NDIS_SIZEOF_LINK_STATE_REVISION_1;
    LinkState.MediaConnectState = MediaConnectStateConnected;
    LinkState.MediaDuplexState = MediaDuplexStateUnknown;
    LinkState.XmitLinkSpeed = 10000000;
    LinkState.RcvLinkSpeed = 10000000;
    LinkState.PauseFunctions = NdisPauseFunctionsUnsupported;

    NdisZeroMemory(&Indication, sizeof(Indication));
    Indication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    Indication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
    Indication.Header.Size = NDIS_SIZEOF_STATUS_INDICATION_REVISION_1;
    Indication.SourceHandle = Adapter->MiniportAdapterHandle;
    Indication.StatusCode = NDIS_STATUS_LINK_STATE;
    Indication.StatusBuffer = &LinkState;
    Indication.StatusBufferSize = sizeof(LinkState);
    NdisMIndicateStatusEx(Adapter->MiniportAdapterHandle, &Indication);

    return NDIS_STATUS_SUCCESS;
}


static NDIS_STATUS NTAPI MiniportSetInformation(
    IN  NDIS_HANDLE MiniportAdapterContext,
    IN  NDIS_OID    Oid,
    IN  PVOID       InformationBuffer,
    IN  ULONG       InformationBufferLength,
    OUT PULONG      BytesRead,
    OUT PULONG      BytesNeeded)
/*
 * FUNCTION: Changes state information in the driver
 * ARGUMENTS:
 *     MiniportAdapterContext  = Pointer to adapter context area
 *     Oid                     = OID code designating set operation
 *     InformationBuffer       = Pointer to buffer with state information
 *     InformationBufferLength = Length of InformationBuffer
 *     BytesRead               = Address of buffer to place number of bytes read
 *     BytesNeeded             = Address of buffer to place number of extra bytes
 *                               needed in InformationBuffer for specified OID
 * RETURNS:
 *     Status of operation
 */
{
    ULONG GenericULONG;
    NDIS_STATUS Status   = NDIS_STATUS_SUCCESS;
    PNIC_ADAPTER Adapter = (PNIC_ADAPTER)MiniportAdapterContext;

    NDIS_DbgPrint(MAX_TRACE, ("Called. Oid (0x%X).\n", Oid));

    switch (Oid) {
    case OID_GEN_CURRENT_PACKET_FILTER:
        /* Verify length */
        if (InformationBufferLength < sizeof(ULONG)) {
            *BytesRead   = 0;
            *BytesNeeded = sizeof(ULONG) - InformationBufferLength;
            Status       = NDIS_STATUS_INVALID_LENGTH;
            break;
        }

        NdisMoveMemory(&GenericULONG, InformationBuffer, sizeof(ULONG));
        /* Check for properties the driver don't support */
        if (GenericULONG &
           (NDIS_PACKET_TYPE_ALL_FUNCTIONAL |
            NDIS_PACKET_TYPE_FUNCTIONAL     |
            NDIS_PACKET_TYPE_GROUP          |
            NDIS_PACKET_TYPE_MAC_FRAME      |
            NDIS_PACKET_TYPE_SMT            |
            NDIS_PACKET_TYPE_SOURCE_ROUTING)) {
            *BytesRead   = 4;
            *BytesNeeded = 0;
            Status       = NDIS_STATUS_NOT_SUPPORTED;
            break;
        }

        Adapter->PacketFilter = GenericULONG;

        /* FIXME: Set filter on hardware */

        break;
    case OID_GEN_CURRENT_LOOKAHEAD:
        /* Verify length */
        if (InformationBufferLength < sizeof(ULONG)) {
            *BytesRead   = 0;
            *BytesNeeded = sizeof(ULONG) - InformationBufferLength;
            Status = NDIS_STATUS_INVALID_LENGTH;
            break;
        }

        NdisMoveMemory(&GenericULONG, InformationBuffer, sizeof(ULONG));
        if (GenericULONG > DRIVER_MAXIMUM_LOOKAHEAD)
            Status = NDIS_STATUS_INVALID_LENGTH;
        else
            Adapter->LookaheadSize = GenericULONG;
        break;
    case OID_802_3_MULTICAST_LIST:
        /* Verify length. Must be multiple of hardware address length */
        if ((InformationBufferLength % DRIVER_LENGTH_OF_ADDRESS) != 0) {
            *BytesRead   = 0;
            *BytesNeeded = 0;
            Status       = NDIS_STATUS_INVALID_LENGTH;
            break;
        }

        /* Set new multicast address list */
        NdisMoveMemory(Adapter->Addresses, InformationBuffer, InformationBufferLength);

        /* FIXME: Update hardware */

        break;
    default:
        NDIS_DbgPrint(MIN_TRACE, ("Invalid object ID (0x%X).\n", Oid));
        *BytesRead   = 0;
        *BytesNeeded = 0;
        Status       = NDIS_STATUS_NOT_SUPPORTED;
        break;
    }

    if (Status == NDIS_STATUS_SUCCESS) {
        *BytesRead   = InformationBufferLength;
        *BytesNeeded = 0;
    }

    NDIS_DbgPrint(MAX_TRACE, ("Leaving. Status (0x%X).\n", Status));

    return Status;
}


static NDIS_STATUS NTAPI MiniportOidRequest(
    IN  NDIS_HANDLE       MiniportAdapterContext,
    IN  PNDIS_OID_REQUEST OidRequest)
{
    switch (OidRequest->RequestType) {
    case NdisRequestQueryInformation:
    case NdisRequestQueryStatistics:
        return MiniportQueryInformation(MiniportAdapterContext,
                                        OidRequest->DATA.QUERY_INFORMATION.Oid,
                                        OidRequest->DATA.QUERY_INFORMATION.InformationBuffer,
                                        OidRequest->DATA.QUERY_INFORMATION.InformationBufferLength,
                                        (PULONG)&OidRequest->DATA.QUERY_INFORMATION.BytesWritten,
                                        (PULONG)&OidRequest->DATA.QUERY_INFORMATION.BytesNeeded);
    case NdisRequestSetInformation:
        return MiniportSetInformation(MiniportAdapterContext,
                                      OidRequest->DATA.SET_INFORMATION.Oid,
                                      OidRequest->DATA.SET_INFORMATION.InformationBuffer,
                                      OidRequest->DATA.SET_INFORMATION.InformationBufferLength,
                                      (PULONG)&OidRequest->DATA.SET_INFORMATION.BytesRead,
                                      (PULONG)&OidRequest->DATA.SET_INFORMATION.BytesNeeded);
    default:
        return NDIS_STATUS_NOT_SUPPORTED;
    }
}


static VOID NTAPI MiniportCancelOidRequest(
    IN  NDIS_HANDLE MiniportAdapterContext,
    IN  PVOID       RequestId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);
}


static VOID NTAPI MiniportDevicePnPEventNotify(
    IN  NDIS_HANDLE           MiniportAdapterContext,
    IN  PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetDevicePnPEvent);
}


static VOID NTAPI MiniportUnload(
    IN  PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    NdisMDeregisterMiniportDriver(DriverInfo.NdisDriverHandle);
}


NTSTATUS
NTAPI
DriverEntry(
    PDRIVER_OBJECT DriverObject,
    PUNICODE_STRING RegistryPath)
/*
 * FUNCTION: Main driver entry point
 * ARGUMENTS:
 *     DriverObject = Pointer to a driver object for this driver
 *     RegistryPath = Registry node for configuration parameters
 * RETURNS:
 *     Status of driver initialization
 */
{
    NDIS_STATUS Status;
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Miniport;

    NDIS_DbgPrint(MAX_TRACE, ("Called.\n"));

    NdisZeroMemory(&Miniport, sizeof(Miniport));
    Miniport.Header.Type                = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    Miniport.Header.Revision            = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
    Miniport.Header.Size                = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
    Miniport.MajorNdisVersion           = NDIS_MINIPORT_MAJOR_VERSION;
    Miniport.MinorNdisVersion           = NDIS_MINIPORT_MINOR_VERSION;
    Miniport.MajorDriverVersion         = DRIVER_VENDOR_DRIVER_VERSION >> 8;
    Miniport.MinorDriverVersion         = DRIVER_VENDOR_DRIVER_VERSION & 0xFF;
    Miniport.InitializeHandlerEx        = MiniportInitializeEx;
    Miniport.HaltHandlerEx              = MiniportHaltEx;
    Miniport.UnloadHandler              = MiniportUnload;
    Miniport.PauseHandler               = MiniportPause;
    Miniport.RestartHandler             = MiniportRestart;
    Miniport.OidRequestHandler          = MiniportOidRequest;
    Miniport.SendNetBufferListsHandler  = MiniportSendNetBufferLists;
    Miniport.ReturnNetBufferListsHandler = MiniportReturnNetBufferLists;
    Miniport.CancelSendHandler          = MiniportCancelSend;
    Miniport.DevicePnPEventNotifyHandler = MiniportDevicePnPEventNotify;
    Miniport.ShutdownHandlerEx          = MiniportShutdownEx;
    Miniport.CancelOidRequestHandler    = MiniportCancelOidRequest;

    InitializeListHead(&DriverInfo.AdapterListHead);

    Status = NdisMRegisterMiniportDriver(DriverObject,
                                         RegistryPath,
                                         NULL,
                                         &Miniport,
                                         &DriverInfo.NdisDriverHandle);
    if (Status != NDIS_STATUS_SUCCESS) {
        NDIS_DbgPrint(MIN_TRACE, ("NdisMRegisterMiniportDriver() failed with status code (0x%X).\n", Status));
        return Status;
    }

    return STATUS_SUCCESS;
}

#if 0
        /* while i'm here - some basic registry sanity checks */
        {
            /* write tests */
            NDIS_CONFIGURATION_PARAMETER ParameterValue;

            ParameterValue.ParameterType = NdisParameterInteger;
            ParameterValue.ParameterData.IntegerData = 0x12345678;
            NdisInitUnicodeString(&Keyword, L"DwordTest");
            NdisWriteConfiguration(&Status, ConfigurationHandle, &Keyword, &ParameterValue);

            if(Status != NDIS_STATUS_SUCCESS)
            {
                DbgPrint("ne2000!MiniportInitialize: failed to set DwordTest: 0x%x\n", Status);
                DbgBreakPoint();
            }

            DbgPrint("ne2000!MiniportInitialize: DwordTest successfully set\n");

            NdisInitUnicodeString(&Keyword, L"StringTest");
            ParameterValue.ParameterType = NdisParameterString;
            NdisInitUnicodeString(&ParameterValue.ParameterData.StringData, L"Testing123");

            NdisWriteConfiguration(&Status, ConfigurationHandle, &Keyword, &ParameterValue);

            if(Status != NDIS_STATUS_SUCCESS)
            {
                DbgPrint("ne2000!MiniportInitialize: failed to set StringTest: 0x%x\n", Status);
                DbgBreakPoint();
            }

            DbgPrint("ne2000!MiniportInitialize: StringTest successfully set\n");
        }

        {
            /* read back the test values */
            NDIS_CONFIGURATION_PARAMETER *ParameterValue = 0;

            NdisInitUnicodeString(&Keyword, L"DwordTest");
            NdisReadConfiguration(&Status, &ParameterValue, ConfigurationHandle, &Keyword, NdisParameterInteger);

            if(Status != NDIS_STATUS_SUCCESS)
            {
                DbgPrint("ne2000!MiniportInitialize: failed to read DwordTest: 0x%x\n", Status);
                DbgBreakPoint();
            }

            if(ParameterValue->ParameterData.IntegerData != 0x12345678)
            {
                DbgPrint("ne2000!MiniportInitialize: DwordTest value is wrong: 0x%x\n",
                    ParameterValue->ParameterData.IntegerData);
                DbgBreakPoint();
            }

            DbgPrint("ne2000!MiniportInitialize: DwordTest value was correctly read\n");

            NdisInitUnicodeString(&Keyword, L"StringTest");
            NdisReadConfiguration(&Status, &ParameterValue, ConfigurationHandle, &Keyword, NdisParameterString);

            if(Status != NDIS_STATUS_SUCCESS)
            {
                DbgPrint("ne2000!MiniportInitialize: failed to read StringTest: 0x%x\n", Status);
                DbgBreakPoint();
            }

            if(wcsncmp(ParameterValue->ParameterData.StringData.Buffer, L"Testing123",
                    wcslen(L"Testing123")))
            {
                DbgPrint("ne2000!MiniportInitialize: StringTest value is wrong: %wZ\n",
                    &ParameterValue->ParameterData.StringData);
                DbgBreakPoint();
            }

            DbgPrint("ne2000!MiniportInitialize: StringTest value was correctly read\n");
        }

#endif

/* EOF */
