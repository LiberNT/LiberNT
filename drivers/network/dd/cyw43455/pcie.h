/*
 * PROJECT:     LiberNT Broadcom/Cypress CYW43xx Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PCIe dongle bus and message buffer protocol definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _CYW_PCIE_H_
#define _CYW_PCIE_H_

#define BCMA_CORE_PCIE2                     0x83C
#define BCMA_CORE_GCI                       0x840
#define BCMA_CORE_ARM_CA7                   0x847

#define CYW_PCI_VENDOR_BROADCOM             0x14E4

#define CYW_PCICFG_BAR0_WINDOW              0x080
#define CYW_PCICFG_INTMASK                  0x094
#define CYW_PCICFG_SBMBX                    0x098
#define CYW_PCICFG_LINK_STATUS_CTRL         0x0BC
#define CYW_PCICFG_ASPM_ENABLE              0x003

#define CYW_BAR0_MAP_LENGTH                 0x8000
#define CYW_BAR0_WINDOW_LENGTH              0x1000
#define CYW_BAR0_PCIE2_REGS                 0x2000

#define PCIE2_REG_MAILBOXINT                0x048
#define PCIE2_REG_MAILBOXMASK               0x04C
#define PCIE2_REG_CONFIGADDR                0x120
#define PCIE2_REG_CONFIGDATA                0x124
#define PCIE2_REG_H2D_MAILBOX_0             0x140
#define PCIE2_REG_H2D_MAILBOX_1             0x144
#define PCIE2_REG64_MAILBOXINT              0xC30
#define PCIE2_REG64_MAILBOXMASK             0xC34
#define PCIE2_REG64_H2D_MAILBOX_0           0xA20
#define PCIE2_REG64_H2D_MAILBOX_1           0xA24
#define PCIE2_MB_INT_FN0                    0x00000300
#define PCIE2_MB_INT_D2H_DB                 0x00FF0000
#define PCIE2_MB64_INT_D2H_DB               0x0000FFFF
#define PCIE2_REG64_FIRST_REV               64
#define PCIE2_CFG_BAR2_CONFIG               0x4E0
#define PCIE2_CFG_TLCNTRL_5                 0x814

#define CC_REG_INTSTATUS                    0x020
#define CC_REG_WATCHDOG                     0x080
#define CC_WD_SSRESET_PCIE_F0_EN            0x10000000
#define CC_WD_SSRESET_PCIE_ALL_FN_EN        0x80000000
#define CC_WD_COUNTER_MASK                  0x0FFFFFFF
#define CC_WD_ENABLE_MASK                   0xF0000000

#define ARMCR4_REG_CAP                      0x04
#define ARMCR4_REG_BANKIDX                  0x40
#define ARMCR4_REG_BANKINFO                 0x44
#define ARMCR4_CAP_TCBANB_MASK              0x0000000F
#define ARMCR4_CAP_TCBBNB_MASK              0x000000F0
#define ARMCR4_CAP_TCBBNB_SHIFT             4
#define ARMCR4_BANK_SIZE_MASK               0x0000007F
#define ARMCR4_BANK_SIZE_UNIT               8192
#define ARMCR4_BANK_1K_UNITS                0x00000200

#define SYSMEM_COREINFO_ROMNB_MASK          0x000003E0
#define SYSMEM_COREINFO_ROMNB_SHIFT         5
#define SYSMEM_COREINFO_SRNB_MASK           0x0000001F
#define SYSMEM_COREINFO_NEW_ROMNB_MASK      0xFF000000
#define SYSMEM_COREINFO_NEW_ROMNB_SHIFT     24
#define SYSMEM_COREINFO_NEW_SRNB_MASK       0x00FF0000
#define SYSMEM_COREINFO_NEW_SRNB_SHIFT      16
#define SYSMEM_NEW_COREINFO_FIRST_REV       12

#define CYW_RAMSIZE_MAGIC                   0x534D4152
#define CYW_RAMSIZE_MAGIC_OFFSET            0x6C

#define CYW_SHARED_FLAGS                    0
#define CYW_SHARED_CONSOLE_ADDR             20
#define CYW_SHARED_MAX_RXBUFPOST            34
#define CYW_SHARED_RX_DATAOFFSET            36
#define CYW_SHARED_HTOD_MB_DATA_ADDR        40
#define CYW_SHARED_DTOH_MB_DATA_ADDR        44
#define CYW_SHARED_RING_INFO_ADDR           48
#define CYW_SHARED_DMA_SCRATCH_LEN          52
#define CYW_SHARED_DMA_SCRATCH_ADDR         56
#define CYW_SHARED_DMA_RINGUPD_LEN          64
#define CYW_SHARED_DMA_RINGUPD_ADDR         68
#define CYW_SHARED_HOST_CAP                 84
#define CYW_SHARED_HOST_CAP2                112

#define CYW_SHARED_VERSION_MASK             0x000000FF
#define CYW_SHARED_MIN_VERSION              5
#define CYW_SHARED_MAX_VERSION              7
#define CYW_SHARED_ITEMSIZE_V7              7
#define CYW_SHARED_DMA_INDEX                0x00010000
#define CYW_SHARED_DMA_2B_IDX               0x00100000
#define CYW_SHARED_USE_MAILBOX              0x02000000
#define CYW_SHARED_HOSTRDY_DB1              0x10000000
#define CYW_SHARED_DAR                      0x80000000

#define CYW_HOSTCAP_H2D_ENABLE_HOSTRDY      0x00000400
#define CYW_HOSTCAP_DS_NO_OOB_DW            0x00001000
#define CYW_HOSTCAP_H2D_DAR                 0x00010000

#define CYW_CONSOLE_BUFADDR                 8
#define CYW_CONSOLE_BUFSIZE                 12
#define CYW_CONSOLE_WRITEIDX                16

#define CYW_RING_DESC_MAX_ITEMS             4
#define CYW_RING_DESC_ITEM_SIZE             6
#define CYW_RING_DESC_BASE_ADDR             8
#define CYW_RING_DESC_SIZE                  16

#define CYW_D2H_DEV_DS_ENTER_REQ            0x00000002
#define CYW_D2H_DEV_FWHALT                  0x10000000
#define CYW_H2D_HOST_DS_ACK                 0x00000002

#define CYW_SCRATCH_LENGTH                  8
#define CYW_RINGUPD_LENGTH                  1024
#define CYW_DEFAULT_MAX_RXBUFPOST           255
#define CYW_MAX_FLOWRINGS                   512
#define CYW_FW_UP_TIMEOUT_MS                5000

#define CYW_FOOTER_HEAP_GAP                 0x1000
#define CYW_FOOTER_HEAP_SIZE                0x10000
#define CYW_RANDOM_SEED_MAGIC               0xFEEDC0DE
#define CYW_RANDOM_SEED_LENGTH              0x100
#define CYW_FW_SIG_MAGIC                    0xFEEDFE51
#define CYW_MEMMAP_MAGIC                    0xFEEDFE53
#define CYW_VSTATUS_MAGIC                   0xFEEDFE54
#define CYW_VSTATUS_LENGTH                  0x28
#define CYW_END_MAGIC                       0xFEED0E2D

#define CYW_OTP_SYS_VENDOR                  0x15
#define CYW_OTP_VENDOR_HEADER               0x00000008
#define CYW_OTP_PARAM_LENGTH                16

#define MSGBUF_TYPE_GEN_STATUS              0x01
#define MSGBUF_TYPE_RING_STATUS             0x02
#define MSGBUF_TYPE_FLOW_RING_CREATE        0x03
#define MSGBUF_TYPE_FLOW_RING_CREATE_CMPLT  0x04
#define MSGBUF_TYPE_FLOW_RING_DELETE        0x05
#define MSGBUF_TYPE_FLOW_RING_DELETE_CMPLT  0x06
#define MSGBUF_TYPE_IOCTLPTR_REQ            0x09
#define MSGBUF_TYPE_IOCTLPTR_REQ_ACK        0x0A
#define MSGBUF_TYPE_IOCTLRESP_BUF_POST      0x0B
#define MSGBUF_TYPE_IOCTL_CMPLT             0x0C
#define MSGBUF_TYPE_EVENT_BUF_POST          0x0D
#define MSGBUF_TYPE_WL_EVENT                0x0E
#define MSGBUF_TYPE_TX_POST                 0x0F
#define MSGBUF_TYPE_TX_STATUS               0x10
#define MSGBUF_TYPE_RXBUF_POST              0x11
#define MSGBUF_TYPE_RX_CMPLT                0x12
#define MSGBUF_TYPE_H2D_MAILBOX_DATA        0x23
#define MSGBUF_TYPE_D2H_MAILBOX_DATA        0x24

#define MSGBUF_PKT_FLAGS_FRAME_802_3        0x01
#define MSGBUF_PKT_FLAGS_FRAME_802_11       0x02
#define MSGBUF_PKT_FLAGS_FRAME_MASK         0x07
#define MSGBUF_PKT_FLAGS_PRIO_SHIFT         5

#define CYW_RING_CONTROL_SUBMIT             0
#define CYW_RING_RXPOST_SUBMIT              1
#define CYW_RING_CONTROL_COMPLETE           2
#define CYW_RING_TX_COMPLETE                3
#define CYW_RING_RX_COMPLETE                4
#define CYW_RING_COMMON_COUNT               5
#define CYW_RING_H2D_COMMON_COUNT           2
#define CYW_RING_D2H_COMMON_COUNT           3
#define CYW_RING_FLOW_FIRST_ID              2

#define CYW_FLOWRING_DEPTH                  512
#define CYW_FLOWRING_ITEM_SIZE              48
#define CYW_FLOWRING_TID_BE                 0

#define CYW_IOCTL_REQUEST_ID                0xFFFE
#define CYW_IOCTL_MAX_INPUT                 1518
#define CYW_CTRL_BUFFER_SIZE                8192
#define CYW_DATA_BUFFER_SIZE                2048
#define CYW_IOCTLRESP_POST_COUNT            8
#define CYW_EVENT_POST_COUNT                8
#define CYW_CTRL_SLOTS                      (CYW_IOCTLRESP_POST_COUNT + CYW_EVENT_POST_COUNT)
#define CYW_RX_SLOTS                        256
#define CYW_TX_SLOTS                        128
#define CYW_POOL_CHUNK_SIZE                 0x10000
#define CYW_POOL_MAX_CHUNKS                 16

#define CYW_PKTID_CTRL_BASE                 0x1000
#define CYW_PKTID_RX_BASE                   0x2000
#define CYW_PKTID_TX_BASE                   0x4000

#define CYW_IOCTL_RESP_TIMEOUT_MS           2000
#define CYW_MSGBUF_RXBUFPOST_THRESHOLD      32

#define CYW_PCIE_POLL_INTERVAL              (-10LL * 10000LL)

typedef enum _CYW_FLOWRING_STATE
{
    CywFlowRingClosed = 0,
    CywFlowRingOpening,
    CywFlowRingOpen,
    CywFlowRingClosing
} CYW_FLOWRING_STATE;

typedef enum _CYW_CTRL_SLOT_KIND
{
    CywCtrlSlotFree = 0,
    CywCtrlSlotIoctlResponse,
    CywCtrlSlotEvent
} CYW_CTRL_SLOT_KIND;

typedef struct _CYW_DMA_BLOCK
{
    PUCHAR Va;
    NDIS_PHYSICAL_ADDRESS Pa;
    ULONG Length;
} CYW_DMA_BLOCK, *PCYW_DMA_BLOCK;

typedef struct _CYW_PKT_POOL
{
    CYW_DMA_BLOCK Chunks[CYW_POOL_MAX_CHUNKS];
    ULONG ChunkCount;
    ULONG ItemSize;
    ULONG ItemsPerChunk;
    ULONG Count;
    ULONG IdBase;
    PUCHAR State;
} CYW_PKT_POOL, *PCYW_PKT_POOL;

typedef struct _CYW_MSGRING
{
    CYW_DMA_BLOCK Memory;
    USHORT Depth;
    USHORT ItemSize;
    USHORT Read;
    USHORT Write;
    ULONG ReadIndex;
    ULONG WriteIndex;
    USHORT Id;
} CYW_MSGRING, *PCYW_MSGRING;

typedef struct _CYW_CORE
{
    USHORT Id;
    UCHAR Rev;
    ULONG Base;
    ULONG Wrap;
} CYW_CORE, *PCYW_CORE;

#define CYW_MAX_CORES                       48

typedef struct _CYW_PCIE
{
    PUCHAR Regs;
    ULONG RegsLength;
    PUCHAR Tcm;
    ULONG TcmLength;
    ULONG Window;
    NDIS_HANDLE InterruptHandle;
    BOOLEAN MessageInterrupt;
    volatile LONG InterruptPending;
    volatile LONG InterruptsEnabled;

    CYW_CORE Cores[CYW_MAX_CORES];
    ULONG CoreCount;
    PCYW_CORE Chipcommon;
    PCYW_CORE Pcie2;
    PCYW_CORE Gci;
    PCYW_CORE Cpu;
    PCYW_CORE Sysmem;

    ULONG MailboxIntReg;
    ULONG MailboxMaskReg;
    ULONG D2hDoorbellMask;
    ULONG Fn0Mask;
    USHORT DeviceId;

    CHAR OtpModule[CYW_OTP_PARAM_LENGTH];
    CHAR OtpVendor[CYW_OTP_PARAM_LENGTH];
    CHAR OtpVersion[CYW_OTP_PARAM_LENGTH];
    BOOLEAN OtpValid;
    CHAR BoardType[64];
    CHAR AntennaSku[16];
    UCHAR MacAddress[6];
    BOOLEAN HaveMacAddress;
    PWSTR ClmPath;
    PWSTR TxcapPath;

    ULONG FirmwareSize;
    BOOLEAN UseSeed;
    BOOLEAN SkipResetVector;

    ULONG SharedAddress;
    ULONG SharedFlags;
    UCHAR SharedVersion;
    USHORT MaxRxBufPost;
    ULONG RxDataOffset;
    ULONG HtodMailboxAddress;
    ULONG DtohMailboxAddress;
    ULONG RingInfoAddress;
    BOOLEAN MailboxViaControl;

    ULONG ConsoleIndexAddress;
    ULONG ConsoleBuffer;
    ULONG ConsoleSize;
    ULONG ConsoleRead;
    CHAR ConsoleLine[160];
    ULONG ConsoleLineLength;
    volatile LONG ConsoleBusy;

    UCHAR IndexSize;
    CYW_DMA_BLOCK IndexBuffer;
    CYW_DMA_BLOCK Scratch;
    CYW_DMA_BLOCK RingUpdate;
    CYW_MSGRING Rings[CYW_RING_COMMON_COUNT];
    USHORT MaxFlowRings;
    USHORT MaxSubmissionRings;
    USHORT MaxCompletionRings;
    ULONG FlowReadIndexBase;
    ULONG FlowWriteIndexBase;
    CYW_MSGRING FlowRing;
    volatile LONG FlowRingState;
    volatile LONG FlowRingRefused;
    KSPIN_LOCK SubmitLock;
    KSPIN_LOCK TxLock;

    CYW_DMA_BLOCK IoctlRequest;
    USHORT IoctlTransId;
    KEVENT IoctlEvent;
    volatile LONG IoctlPending;
    LONG IoctlFirmwareStatus;
    BOOLEAN IoctlResponseLost;
    ULONG IoctlResponseLength;
    PUCHAR IoctlResponse;

    CYW_PKT_POOL CtrlPool;
    CYW_PKT_POOL RxPool;
    CYW_PKT_POOL TxPool;
    ULONG IoctlRespPosted;
    ULONG EventPosted;
    volatile LONG FirmwareFailed;
    volatile LONG Recovering;
    ULONG RxPosted;
    ULONG RxPostTarget;
    ULONG RxCompletedSinceUpdate;
    volatile LONG TxPosted;

    volatile LONG Up;
    BOOLEAN FirmwareHalted;
} CYW_PCIE, *PCYW_PCIE;

#pragma pack(push, 1)

typedef struct _MSGBUF_ADDR
{
    ULONG Low;
    ULONG High;
} MSGBUF_ADDR, *PMSGBUF_ADDR;

typedef struct _MSGBUF_HEADER
{
    UCHAR MsgType;
    UCHAR IfIdx;
    UCHAR Flags;
    UCHAR Reserved;
    ULONG RequestId;
} MSGBUF_HEADER, *PMSGBUF_HEADER;

typedef struct _MSGBUF_COMPLETION
{
    USHORT Status;
    USHORT FlowRingId;
} MSGBUF_COMPLETION, *PMSGBUF_COMPLETION;

typedef struct _MSGBUF_IOCTL_REQUEST
{
    MSGBUF_HEADER Header;
    ULONG Cmd;
    USHORT TransId;
    USHORT InputLength;
    USHORT OutputLength;
    USHORT Reserved0[3];
    MSGBUF_ADDR Buffer;
    ULONG Reserved1[2];
} MSGBUF_IOCTL_REQUEST, *PMSGBUF_IOCTL_REQUEST;

typedef struct _MSGBUF_CTRL_POST
{
    MSGBUF_HEADER Header;
    USHORT BufferLength;
    USHORT Reserved0[3];
    MSGBUF_ADDR Buffer;
    ULONG Reserved1[4];
} MSGBUF_CTRL_POST, *PMSGBUF_CTRL_POST;

typedef struct _MSGBUF_RX_POST
{
    MSGBUF_HEADER Header;
    USHORT MetadataLength;
    USHORT DataLength;
    ULONG Reserved;
    MSGBUF_ADDR Metadata;
    MSGBUF_ADDR Data;
} MSGBUF_RX_POST, *PMSGBUF_RX_POST;

typedef struct _MSGBUF_TX_POST
{
    MSGBUF_HEADER Header;
    UCHAR EtherHeader[14];
    UCHAR Flags;
    UCHAR SegmentCount;
    MSGBUF_ADDR Metadata;
    MSGBUF_ADDR Data;
    USHORT MetadataLength;
    USHORT DataLength;
    ULONG Reserved;
} MSGBUF_TX_POST, *PMSGBUF_TX_POST;

typedef struct _MSGBUF_FLOWRING_CREATE
{
    MSGBUF_HEADER Header;
    UCHAR Da[6];
    UCHAR Sa[6];
    UCHAR Tid;
    UCHAR IfFlags;
    USHORT FlowRingId;
    UCHAR Tc;
    UCHAR Priority;
    USHORT IntVector;
    USHORT MaxItems;
    USHORT ItemSize;
    MSGBUF_ADDR RingAddress;
} MSGBUF_FLOWRING_CREATE, *PMSGBUF_FLOWRING_CREATE;

typedef struct _MSGBUF_FLOWRING_DELETE
{
    MSGBUF_HEADER Header;
    USHORT FlowRingId;
    USHORT Reason;
    ULONG Reserved[7];
} MSGBUF_FLOWRING_DELETE, *PMSGBUF_FLOWRING_DELETE;

typedef struct _MSGBUF_H2D_MAILBOX
{
    MSGBUF_HEADER Header;
    ULONG Data;
    ULONG Reserved[7];
} MSGBUF_H2D_MAILBOX, *PMSGBUF_H2D_MAILBOX;

typedef struct _MSGBUF_IOCTL_COMPLETE
{
    MSGBUF_HEADER Header;
    MSGBUF_COMPLETION Completion;
    USHORT ResponseLength;
    USHORT TransId;
    ULONG Cmd;
    ULONG Reserved;
} MSGBUF_IOCTL_COMPLETE, *PMSGBUF_IOCTL_COMPLETE;

typedef struct _MSGBUF_EVENT
{
    MSGBUF_HEADER Header;
    MSGBUF_COMPLETION Completion;
    USHORT EventLength;
    USHORT Sequence;
    USHORT Reserved[4];
} MSGBUF_EVENT, *PMSGBUF_EVENT;

typedef struct _MSGBUF_TX_STATUS
{
    MSGBUF_HEADER Header;
    MSGBUF_COMPLETION Completion;
    USHORT MetadataLength;
    USHORT TxStatus;
} MSGBUF_TX_STATUS, *PMSGBUF_TX_STATUS;

typedef struct _MSGBUF_RX_COMPLETE
{
    MSGBUF_HEADER Header;
    MSGBUF_COMPLETION Completion;
    USHORT MetadataLength;
    USHORT DataLength;
    USHORT DataOffset;
    USHORT Flags;
    ULONG RxStatus0;
    ULONG RxStatus1;
    ULONG Reserved;
} MSGBUF_RX_COMPLETE, *PMSGBUF_RX_COMPLETE;

typedef struct _MSGBUF_STATUS
{
    MSGBUF_HEADER Header;
    MSGBUF_COMPLETION Completion;
    USHORT WriteIndex;
} MSGBUF_STATUS, *PMSGBUF_STATUS;

typedef struct _MSGBUF_D2H_MAILBOX
{
    MSGBUF_HEADER Header;
    MSGBUF_COMPLETION Completion;
    ULONG Data;
    ULONG Reserved[2];
} MSGBUF_D2H_MAILBOX, *PMSGBUF_D2H_MAILBOX;

typedef struct _CYW_PCIE_RING_INFO
{
    ULONG RingMem;
    ULONG H2dWriteIndexPtr;
    ULONG H2dReadIndexPtr;
    ULONG D2hWriteIndexPtr;
    ULONG D2hReadIndexPtr;
    MSGBUF_ADDR H2dWriteIndexHost;
    MSGBUF_ADDR H2dReadIndexHost;
    MSGBUF_ADDR D2hWriteIndexHost;
    MSGBUF_ADDR D2hReadIndexHost;
    USHORT MaxFlowRings;
    USHORT MaxSubmissionRings;
    USHORT MaxCompletionRings;
} CYW_PCIE_RING_INFO, *PCYW_PCIE_RING_INFO;

typedef struct _CYW_FW_FOOTER
{
    ULONG Length;
    ULONG Magic;
} CYW_FW_FOOTER, *PCYW_FW_FOOTER;

typedef struct _CYW_FW_REGION
{
    ULONG Start;
    ULONG End;
} CYW_FW_REGION;

typedef struct _CYW_FW_MEMMAP
{
    CYW_FW_REGION ResetVector;
    CYW_FW_REGION InterruptVector;
    CYW_FW_REGION Rom;
    CYW_FW_REGION Memmap;
    CYW_FW_REGION VerificationStatus;
    CYW_FW_REGION Firmware;
    CYW_FW_REGION Signature;
    CYW_FW_REGION Heap;
    CYW_FW_REGION Stack;
    CYW_FW_REGION Prng;
    CYW_FW_REGION Nvram;
} CYW_FW_MEMMAP, *PCYW_FW_MEMMAP;

#pragma pack(pop)

C_ASSERT(sizeof(MSGBUF_HEADER) == 8);
C_ASSERT(sizeof(MSGBUF_IOCTL_REQUEST) == 40);
C_ASSERT(sizeof(MSGBUF_CTRL_POST) == 40);
C_ASSERT(sizeof(MSGBUF_RX_POST) == 32);
C_ASSERT(sizeof(MSGBUF_TX_POST) == 48);
C_ASSERT(sizeof(MSGBUF_FLOWRING_CREATE) == 40);
C_ASSERT(sizeof(MSGBUF_FLOWRING_DELETE) == 40);
C_ASSERT(sizeof(MSGBUF_H2D_MAILBOX) == 40);
C_ASSERT(sizeof(MSGBUF_IOCTL_COMPLETE) == 24);
C_ASSERT(sizeof(MSGBUF_EVENT) == 24);
C_ASSERT(sizeof(MSGBUF_TX_STATUS) == 16);
C_ASSERT(sizeof(MSGBUF_RX_COMPLETE) == 32);
C_ASSERT(sizeof(MSGBUF_D2H_MAILBOX) == 24);
C_ASSERT(sizeof(CYW_PCIE_RING_INFO) == 58);
C_ASSERT(sizeof(CYW_FW_MEMMAP) == 88);

BOOLEAN CywPcieTcmRange(_In_ PCYW_PCIE Pcie, _In_ ULONG Address, _In_ ULONG Length);
ULONG CywPcieReadTcm32(_In_ PCYW_PCIE Pcie, _In_ ULONG Address);
VOID CywPcieWriteTcm32(_In_ PCYW_PCIE Pcie, _In_ ULONG Address, _In_ ULONG Value);
USHORT CywPcieReadTcm16(_In_ PCYW_PCIE Pcie, _In_ ULONG Address);
VOID CywPcieWriteTcm16(_In_ PCYW_PCIE Pcie, _In_ ULONG Address, _In_ USHORT Value);
VOID CywPcieRingDoorbell(_In_ PCYW_PCIE Pcie);
VOID CywPcieSetInterrupts(_In_ PCYW_PCIE Pcie, _In_ BOOLEAN Enable);
VOID CywPcieHandleMailbox(_In_ PCYW_ADAPTER Adapter, _In_ ULONG Data);
VOID CywPcieServiceMailbox(_In_ PCYW_ADAPTER Adapter);
VOID CywPcieReadConsole(_In_ PCYW_PCIE Pcie);
NTSTATUS CywPcieAllocateDma(_In_ PCYW_ADAPTER Adapter, _In_ ULONG Length, _Out_ PCYW_DMA_BLOCK Block);
VOID CywPcieFreeDma(_In_ PCYW_ADAPTER Adapter, _Inout_ PCYW_DMA_BLOCK Block);

NTSTATUS CywMsgbufInitialize(_In_ PCYW_ADAPTER Adapter);
VOID CywMsgbufRelease(_In_ PCYW_ADAPTER Adapter);
VOID CywMsgbufService(_In_ PCYW_ADAPTER Adapter);
NTSTATUS CywMsgbufSendMailbox(_In_ PCYW_ADAPTER Adapter, _In_ ULONG Data);

#endif /* _CYW_PCIE_H_ */
