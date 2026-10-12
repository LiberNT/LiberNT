/*
 * PROJECT:     LiberNT Storage Drivers
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     LSI Fusion-MPT MPI 1.5 message interface subset
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

#define MPI_VERSION_01_05                       0x0105
#define MPI_HEADER_VERSION                      0x1400

#define MPI_IOC_STATE_RESET                     0x00000000
#define MPI_IOC_STATE_READY                     0x10000000
#define MPI_IOC_STATE_OPERATIONAL               0x20000000
#define MPI_IOC_STATE_FAULT                     0x40000000
#define MPI_IOC_STATE_MASK                      0xF0000000

#define MPI_DOORBELL_OFFSET                     0x00
#define MPI_WRITE_SEQUENCE_OFFSET               0x04
#define MPI_DIAGNOSTIC_OFFSET                   0x08
#define MPI_HOST_INTERRUPT_STATUS_OFFSET        0x30
#define MPI_HOST_INTERRUPT_MASK_OFFSET          0x34
#define MPI_REQUEST_POST_FIFO_OFFSET            0x40
#define MPI_REPLY_POST_FIFO_OFFSET              0x44
#define MPI_REPLY_FREE_FIFO_OFFSET              0x44

#define MPI_DOORBELL_ACTIVE                     0x08000000
#define MPI_DOORBELL_FUNCTION_SHIFT             24
#define MPI_DOORBELL_ADD_DWORDS_SHIFT           16
#define MPI_DOORBELL_DATA_MASK                  0x0000FFFF

#define MPI_WRSEQ_KEY_VALUE_MASK                0x0000000F
#define MPI_WRSEQ_1ST_KEY_VALUE                 0x04
#define MPI_WRSEQ_2ND_KEY_VALUE                 0x0B
#define MPI_WRSEQ_3RD_KEY_VALUE                 0x02
#define MPI_WRSEQ_4TH_KEY_VALUE                 0x07
#define MPI_WRSEQ_5TH_KEY_VALUE                 0x0D

#define MPI_DIAG_DRWE                           0x00000080
#define MPI_DIAG_RESET_ADAPTER                  0x00000004

#define MPI_HIS_IOP_DOORBELL_STATUS             0x80000000
#define MPI_HIS_REPLY_MESSAGE_INTERRUPT         0x00000008
#define MPI_HIS_DOORBELL_INTERRUPT              0x00000001

#define MPI_HIM_RIM                             0x00000008
#define MPI_HIM_DIM                             0x00000001

#define MPI_ADDRESS_REPLY_A_BIT                 0x80000000
#define MPI_CONTEXT_REPLY_TYPE_MASK             0x60000000
#define MPI_CONTEXT_REPLY_TYPE_SHIFT            29
#define MPI_CONTEXT_REPLY_TYPE_SCSI_INIT        0x00
#define MPI_CONTEXT_REPLY_CONTEXT_MASK          0x1FFFFFFF

#define MPI_FUNCTION_SCSI_IO_REQUEST            0x00
#define MPI_FUNCTION_SCSI_TASK_MGMT             0x01
#define MPI_FUNCTION_IOC_INIT                   0x02
#define MPI_FUNCTION_IOC_FACTS                  0x03
#define MPI_FUNCTION_PORT_FACTS                 0x05
#define MPI_FUNCTION_PORT_ENABLE                0x06
#define MPI_FUNCTION_IOC_MESSAGE_UNIT_RESET     0x40
#define MPI_FUNCTION_HANDSHAKE                  0x42

#define MPI_SGE_FLAGS_LAST_ELEMENT              0x80
#define MPI_SGE_FLAGS_END_OF_BUFFER             0x40
#define MPI_SGE_FLAGS_SIMPLE_ELEMENT            0x10
#define MPI_SGE_FLAGS_CHAIN_ELEMENT             0x30
#define MPI_SGE_FLAGS_HOST_TO_IOC               0x04
#define MPI_SGE_FLAGS_64_BIT_ADDRESSING         0x02
#define MPI_SGE_FLAGS_END_OF_LIST               0x01
#define MPI_SGE_FLAGS_SHIFT                     24
#define MPI_SGE_LENGTH_MASK                     0x00FFFFFF

#define MPI_IOCSTATUS_SUCCESS                   0x0000
#define MPI_IOCSTATUS_BUSY                      0x0002
#define MPI_IOCSTATUS_INSUFFICIENT_RESOURCES    0x0006
#define MPI_IOCSTATUS_SCSI_RECOVERED_ERROR      0x0040
#define MPI_IOCSTATUS_SCSI_INVALID_BUS          0x0041
#define MPI_IOCSTATUS_SCSI_INVALID_TARGETID     0x0042
#define MPI_IOCSTATUS_SCSI_DEVICE_NOT_THERE     0x0043
#define MPI_IOCSTATUS_SCSI_DATA_OVERRUN         0x0044
#define MPI_IOCSTATUS_SCSI_DATA_UNDERRUN        0x0045
#define MPI_IOCSTATUS_SCSI_TASK_TERMINATED      0x0048
#define MPI_IOCSTATUS_SCSI_RESIDUAL_MISMATCH    0x0049
#define MPI_IOCSTATUS_SCSI_IOC_TERMINATED       0x004B
#define MPI_IOCSTATUS_SCSI_EXT_TERMINATED       0x004C
#define MPI_IOCSTATUS_MASK                      0x7FFF

#define MPI_WHOINIT_HOST_DRIVER                 0x04

#define MPI_PORTFACTS_PORTTYPE_INACTIVE         0x00

#define MPI_SCSIIO_MSGFLGS_SENSE_WIDTH_32       0x00
#define MPI_SCSIIO_MSGFLGS_SENSE_LOC_HOST       0x00

#define MPI_SCSIIO_CONTROL_NODATATRANSFER       0x00000000
#define MPI_SCSIIO_CONTROL_WRITE                0x01000000
#define MPI_SCSIIO_CONTROL_READ                 0x02000000
#define MPI_SCSIIO_CONTROL_SIMPLEQ              0x00000000
#define MPI_SCSIIO_CONTROL_HEADOFQ              0x00000100
#define MPI_SCSIIO_CONTROL_ORDEREDQ             0x00000200

#define MPI_SCSI_STATUS_SUCCESS                 0x00
#define MPI_SCSI_STATUS_CHECK_CONDITION         0x02

#define MPI_SCSI_STATE_AUTOSENSE_VALID          0x01
#define MPI_SCSI_STATE_NO_SCSI_STATUS           0x04
#define MPI_SCSI_STATE_TERMINATED               0x08

#define MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK            0x01
#define MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET          0x03
#define MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS             0x04
#define MPI_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET    0x05

#define MPI_SCSITASKMGMT_RSP_TM_COMPLETE                0x00
#define MPI_SCSITASKMGMT_RSP_TM_SUCCEEDED               0x08

#define MPI_MANUFACTPAGE_DEVID_SAS1064          0x0050
#define MPI_MANUFACTPAGE_DEVID_SAS1068          0x0054
#define MPI_MANUFACTPAGE_DEVID_SAS1064E         0x0056
#define MPI_MANUFACTPAGE_DEVID_SAS1068E         0x0058

typedef struct _MPI_SGE_SIMPLE64
{
    ULONG FlagsLength;
    ULONG AddressLow;
    ULONG AddressHigh;
} MPI_SGE_SIMPLE64, *PMPI_SGE_SIMPLE64;

typedef struct _MPI_SGE_CHAIN64
{
    USHORT Length;
    UCHAR NextChainOffset;
    UCHAR Flags;
    ULONG AddressLow;
    ULONG AddressHigh;
} MPI_SGE_CHAIN64, *PMPI_SGE_CHAIN64;

typedef struct _MPI_REQUEST_HEADER
{
    UCHAR Reserved[2];
    UCHAR ChainOffset;
    UCHAR Function;
    UCHAR Reserved1[3];
    UCHAR MsgFlags;
    ULONG MsgContext;
} MPI_REQUEST_HEADER, *PMPI_REQUEST_HEADER;

typedef struct _MPI_DEFAULT_REPLY
{
    UCHAR Reserved[2];
    UCHAR MsgLength;
    UCHAR Function;
    UCHAR Reserved1[3];
    UCHAR MsgFlags;
    ULONG MsgContext;
    UCHAR Reserved2[2];
    USHORT IOCStatus;
    ULONG IOCLogInfo;
} MPI_DEFAULT_REPLY, *PMPI_DEFAULT_REPLY;

typedef struct _MPI_IOC_FACTS_REPLY
{
    USHORT MsgVersion;
    UCHAR MsgLength;
    UCHAR Function;
    USHORT HeaderVersion;
    UCHAR IOCNumber;
    UCHAR MsgFlags;
    ULONG MsgContext;
    USHORT IOCExceptions;
    USHORT IOCStatus;
    ULONG IOCLogInfo;
    UCHAR MaxChainDepth;
    UCHAR WhoInit;
    UCHAR BlockSize;
    UCHAR Flags;
    USHORT ReplyQueueDepth;
    USHORT RequestFrameSize;
    USHORT Reserved_0101_FWVersion;
    USHORT ProductID;
    ULONG CurrentHostMfaHighAddr;
    USHORT GlobalCredits;
    UCHAR NumberOfPorts;
    UCHAR EventState;
    ULONG CurrentSenseBufferHighAddr;
    USHORT CurReplyFrameSize;
    UCHAR MaxDevices;
    UCHAR MaxBuses;
    ULONG FWImageSize;
    ULONG IOCCapabilities;
    ULONG FWVersion;
    USHORT HighPriorityQueueDepth;
    USHORT Reserved2;
    MPI_SGE_SIMPLE64 HostPageBufferSGE;
    ULONG ReplyFifoHostSignalingAddr;
} MPI_IOC_FACTS_REPLY, *PMPI_IOC_FACTS_REPLY;

typedef struct _MPI_IOC_INIT_REQUEST
{
    UCHAR WhoInit;
    UCHAR Reserved;
    UCHAR ChainOffset;
    UCHAR Function;
    UCHAR Flags;
    UCHAR MaxDevices;
    UCHAR MaxBuses;
    UCHAR MsgFlags;
    ULONG MsgContext;
    USHORT ReplyFrameSize;
    UCHAR Reserved1[2];
    ULONG HostMfaHighAddr;
    ULONG SenseBufferHighAddr;
    ULONG ReplyFifoHostSignalingAddr;
    MPI_SGE_SIMPLE64 HostPageBufferSGE;
    USHORT MsgVersion;
    USHORT HeaderVersion;
} MPI_IOC_INIT_REQUEST, *PMPI_IOC_INIT_REQUEST;

typedef struct _MPI_PORT_REQUEST
{
    UCHAR Reserved[2];
    UCHAR ChainOffset;
    UCHAR Function;
    UCHAR Reserved1[2];
    UCHAR PortNumber;
    UCHAR MsgFlags;
    ULONG MsgContext;
} MPI_PORT_REQUEST, *PMPI_PORT_REQUEST;

typedef struct _MPI_PORT_FACTS_REPLY
{
    USHORT Reserved;
    UCHAR MsgLength;
    UCHAR Function;
    USHORT Reserved1;
    UCHAR PortNumber;
    UCHAR MsgFlags;
    ULONG MsgContext;
    USHORT Reserved2;
    USHORT IOCStatus;
    ULONG IOCLogInfo;
    UCHAR Reserved3;
    UCHAR PortType;
    USHORT MaxDevices;
    USHORT PortSCSIID;
    USHORT ProtocolFlags;
    USHORT MaxPostedCmdBuffers;
    USHORT MaxPersistentIDs;
    USHORT MaxLanBuckets;
    UCHAR MaxInitiators;
    UCHAR Reserved4;
    ULONG Reserved5;
} MPI_PORT_FACTS_REPLY, *PMPI_PORT_FACTS_REPLY;

typedef struct _MPI_SCSI_IO_REQUEST
{
    UCHAR TargetID;
    UCHAR Bus;
    UCHAR ChainOffset;
    UCHAR Function;
    UCHAR CDBLength;
    UCHAR SenseBufferLength;
    UCHAR Reserved;
    UCHAR MsgFlags;
    ULONG MsgContext;
    UCHAR LUN[8];
    ULONG Control;
    UCHAR CDB[16];
    ULONG DataLength;
    ULONG SenseBufferLowAddr;
} MPI_SCSI_IO_REQUEST, *PMPI_SCSI_IO_REQUEST;

typedef struct _MPI_SCSI_IO_REPLY
{
    UCHAR TargetID;
    UCHAR Bus;
    UCHAR MsgLength;
    UCHAR Function;
    UCHAR CDBLength;
    UCHAR SenseBufferLength;
    UCHAR Reserved;
    UCHAR MsgFlags;
    ULONG MsgContext;
    UCHAR SCSIStatus;
    UCHAR SCSIState;
    USHORT IOCStatus;
    ULONG IOCLogInfo;
    ULONG TransferCount;
    ULONG SenseCount;
    ULONG ResponseInfo;
    USHORT TaskTag;
    USHORT Reserved1;
} MPI_SCSI_IO_REPLY, *PMPI_SCSI_IO_REPLY;

typedef struct _MPI_SCSI_TASK_MGMT_REQUEST
{
    UCHAR TargetID;
    UCHAR Bus;
    UCHAR ChainOffset;
    UCHAR Function;
    UCHAR Reserved;
    UCHAR TaskType;
    UCHAR Reserved1;
    UCHAR MsgFlags;
    ULONG MsgContext;
    UCHAR LUN[8];
    ULONG Reserved2[7];
    ULONG TaskMsgContext;
} MPI_SCSI_TASK_MGMT_REQUEST, *PMPI_SCSI_TASK_MGMT_REQUEST;

typedef struct _MPI_SCSI_TASK_MGMT_REPLY
{
    UCHAR TargetID;
    UCHAR Bus;
    UCHAR MsgLength;
    UCHAR Function;
    UCHAR ResponseCode;
    UCHAR TaskType;
    UCHAR Reserved1;
    UCHAR MsgFlags;
    ULONG MsgContext;
    UCHAR Reserved2[2];
    USHORT IOCStatus;
    ULONG IOCLogInfo;
    ULONG TerminationCount;
} MPI_SCSI_TASK_MGMT_REPLY, *PMPI_SCSI_TASK_MGMT_REPLY;

C_ASSERT(sizeof(MPI_SGE_SIMPLE64) == 0x0C);
C_ASSERT(sizeof(MPI_SGE_CHAIN64) == 0x0C);
C_ASSERT(sizeof(MPI_REQUEST_HEADER) == 0x0C);
C_ASSERT(sizeof(MPI_DEFAULT_REPLY) == 0x14);
C_ASSERT(FIELD_OFFSET(MPI_DEFAULT_REPLY, IOCStatus) == 0x0E);
C_ASSERT(FIELD_OFFSET(MPI_IOC_FACTS_REPLY, MaxChainDepth) == 0x14);
C_ASSERT(FIELD_OFFSET(MPI_IOC_FACTS_REPLY, ReplyQueueDepth) == 0x18);
C_ASSERT(FIELD_OFFSET(MPI_IOC_FACTS_REPLY, GlobalCredits) == 0x24);
C_ASSERT(FIELD_OFFSET(MPI_IOC_FACTS_REPLY, MaxDevices) == 0x2E);
C_ASSERT(FIELD_OFFSET(MPI_IOC_FACTS_REPLY, FWVersion) == 0x38);
C_ASSERT(FIELD_OFFSET(MPI_IOC_FACTS_REPLY, HostPageBufferSGE) == 0x40);
C_ASSERT(sizeof(MPI_IOC_FACTS_REPLY) == 0x50);
C_ASSERT(FIELD_OFFSET(MPI_IOC_INIT_REQUEST, ReplyFrameSize) == 0x0C);
C_ASSERT(FIELD_OFFSET(MPI_IOC_INIT_REQUEST, HostPageBufferSGE) == 0x1C);
C_ASSERT(FIELD_OFFSET(MPI_IOC_INIT_REQUEST, MsgVersion) == 0x28);
C_ASSERT(sizeof(MPI_IOC_INIT_REQUEST) == 0x2C);
C_ASSERT(sizeof(MPI_PORT_REQUEST) == 0x0C);
C_ASSERT(FIELD_OFFSET(MPI_PORT_FACTS_REPLY, PortType) == 0x15);
C_ASSERT(FIELD_OFFSET(MPI_PORT_FACTS_REPLY, PortSCSIID) == 0x18);
C_ASSERT(sizeof(MPI_PORT_FACTS_REPLY) == 0x28);
C_ASSERT(FIELD_OFFSET(MPI_SCSI_IO_REQUEST, LUN) == 0x0C);
C_ASSERT(FIELD_OFFSET(MPI_SCSI_IO_REQUEST, Control) == 0x14);
C_ASSERT(FIELD_OFFSET(MPI_SCSI_IO_REQUEST, CDB) == 0x18);
C_ASSERT(FIELD_OFFSET(MPI_SCSI_IO_REQUEST, DataLength) == 0x28);
C_ASSERT(sizeof(MPI_SCSI_IO_REQUEST) == 0x30);
C_ASSERT(FIELD_OFFSET(MPI_SCSI_IO_REPLY, SCSIStatus) == 0x0C);
C_ASSERT(FIELD_OFFSET(MPI_SCSI_IO_REPLY, TransferCount) == 0x14);
C_ASSERT(FIELD_OFFSET(MPI_SCSI_IO_REPLY, TaskTag) == 0x20);
C_ASSERT(sizeof(MPI_SCSI_IO_REPLY) == 0x24);
C_ASSERT(FIELD_OFFSET(MPI_SCSI_TASK_MGMT_REQUEST, TaskMsgContext) == 0x30);
C_ASSERT(sizeof(MPI_SCSI_TASK_MGMT_REQUEST) == 0x34);
C_ASSERT(sizeof(MPI_SCSI_TASK_MGMT_REPLY) == 0x18);
