/*
 * PROJECT:     LiberNT Ralink RT2800 USB Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver definitions for the RT2870/RT2770 USB wireless adapters
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <ndis.h>
#include <windot11.h>
#include <usb.h>
#include <usbdlib.h>
#include <usbioctl.h>

#define RT_TAG                          'U82R'

#define RT_ADDRESS_LENGTH               6
#define RT_MTU_SIZE                     1500
#define RT_MAX_80211_FRAME              2346
#define RT_MAX_BSS                      48
#define RT_MAX_IE_BYTES                 768
#define RT_RX_URBS                      8
#define RT_RX_BUFFER_SIZE               16384
#define RT_TX_URBS                      16
#define RT_HELD_RX_MAX                  8
#define RT_TX_BUFFER_SIZE               2560
#define RT_EEPROM_WORDS                 256
#define RT_FIRMWARE_SIZE                8192
#define RT_FIRMWARE_HALF                4096
#define RT_FIRMWARE_PATH                L"\\SystemRoot\\System32\\drivers\\ralink\\rt2870.bin"
#define RT_LINK_SPEED_BPS               54000000ULL
#define RT_SCAN_DWELL_MS                120
#define RT_AUTH_TIMEOUT_MS              300
#define RT_ASSOC_TIMEOUT_MS             300
#define RT_MLME_RETRIES                 3
#define RT_ASSOC_COMEBACK_MAX           8
#define RT_SA_QUERY_MAX_TIMEOUT_MS      1024
#define RT_BEACON_LOSS_MS               8000
#define RT_AP_WCID                      33

#define RT_USB_DEVICE_MODE              1
#define RT_USB_SINGLE_WRITE             2
#define RT_USB_MULTI_WRITE              6
#define RT_USB_MULTI_READ               7
#define RT_USB_EEPROM_READ              9
#define RT_USB_MODE_RESET               1
#define RT_USB_MODE_FIRMWARE            8
#define RT_USB_MODE_AUTORUN             17

#define RT_FIRMWARE_IMAGE_BASE          0x3000

#define RT_MAC_CSR0                     0x1000
#define RT_MAC_SYS_CTRL                 0x1004
#define RT_MAC_ADDR_DW0                 0x1008
#define RT_MAC_ADDR_DW1                 0x100c
#define RT_MAC_BSSID_DW0                0x1010
#define RT_MAC_BSSID_DW1                0x1014
#define RT_MAX_LEN_CFG                  0x1018
#define RT_BBP_CSR_CFG                  0x101c
#define RT_RF_CSR_CFG0                  0x1020
#define RT_LED_CFG                      0x102c
#define RT_AMPDU_BA_WINSIZE             0x1040
#define RT_XIFS_TIME_CFG                0x1100
#define RT_BKOFF_SLOT_CFG               0x1104
#define RT_CH_TIME_CFG                  0x110c
#define RT_BCN_TIME_CFG                 0x1114
#define RT_TBTT_SYNC_CFG                0x1118
#define RT_INT_TIMER_CFG                0x1128
#define RT_CH_IDLE_STA                  0x1130
#define RT_CH_BUSY_STA                  0x1134
#define RT_CH_BUSY_STA_SEC              0x1138
#define RT_MAC_STATUS_CFG               0x1200
#define RT_PWR_PIN_CFG                  0x1204
#define RT_AUTOWAKEUP_CFG               0x1208
#define RT_EDCA_AC0_CFG                 0x1300
#define RT_TX_PWR_CFG_0                 0x1314
#define RT_TX_PIN_CFG                   0x1328
#define RT_TX_BAND_CFG                  0x132c
#define RT_TX_SW_CFG0                   0x1330
#define RT_TX_SW_CFG1                   0x1334
#define RT_TXOP_CTRL_CFG                0x1340
#define RT_TX_RTS_CFG                   0x1344
#define RT_TX_TIMEOUT_CFG               0x1348
#define RT_TX_RTY_CFG                   0x134c
#define RT_TX_LINK_CFG                  0x1350
#define RT_HT_FBK_CFG0                  0x1354
#define RT_HT_FBK_CFG1                  0x1358
#define RT_LG_FBK_CFG0                  0x135c
#define RT_LG_FBK_CFG1                  0x1360
#define RT_CCK_PROT_CFG                 0x1364
#define RT_OFDM_PROT_CFG                0x1368
#define RT_MM20_PROT_CFG                0x136c
#define RT_MM40_PROT_CFG                0x1370
#define RT_GF20_PROT_CFG                0x1374
#define RT_GF40_PROT_CFG                0x1378
#define RT_EXP_ACK_TIME                 0x1380
#define RT_RX_FILTER_CFG                0x1400
#define RT_AUTO_RSP_CFG                 0x1404
#define RT_LEGACY_BASIC_RATE            0x1408
#define RT_HT_BASIC_RATE                0x140c
#define RT_TXOP_HLDR_ET                 0x1608
#define RT_RX_STA_CNT0                  0x1700
#define RT_RX_STA_CNT1                  0x1704
#define RT_RX_STA_CNT2                  0x1708
#define RT_TX_STA_CNT0                  0x170c
#define RT_TX_STA_CNT1                  0x1710
#define RT_TX_STA_CNT2                  0x1714
#define RT_TX_STA_FIFO                  0x1718
#define RT_MAC_WCID_BASE                0x1800
#define RT_PAIRWISE_KEY_BASE            0x4000
#define RT_MAC_IVEIV_BASE               0x6000
#define RT_MAC_WCID_ATTR_BASE           0x6800
#define RT_SHARED_KEY_BASE              0x6c00
#define RT_SHARED_KEY_MODE_BASE         0x7000
#define RT_H2M_MAILBOX_CSR              0x7010
#define RT_H2M_MAILBOX_CID              0x7014
#define RT_H2M_MAILBOX_STATUS           0x701c
#define RT_H2M_INT_SRC                  0x7024
#define RT_H2M_BBP_AGENT                0x7028
#define RT_HW_BEACON_BASE0              0x7800
#define RT_INT_SOURCE_CSR               0x0200
#define RT_WPDMA_GLO_CFG                0x0208
#define RT_USB_DMA_CFG                  0x02a0
#define RT_US_CYC_CNT                   0x02a4
#define RT_PBF_SYS_CTRL                 0x0400
#define RT_HOST_CMD_CSR                 0x0404
#define RT_PBF_CFG                      0x0408
#define RT_PBF_MAX_PCNT                 0x040c
#define RT_EFUSE_CTRL                   0x0580

#define RT_MAC_SYS_CTRL_RESET_CSR       0x00000001
#define RT_MAC_SYS_CTRL_RESET_BBP       0x00000002
#define RT_MAC_SYS_CTRL_ENABLE_TX       0x00000004
#define RT_MAC_SYS_CTRL_ENABLE_RX       0x00000008

#define RT_BBP_CSR_CFG_VALUE            0x000000ff
#define RT_BBP_CSR_CFG_REGNUM_SHIFT     8
#define RT_BBP_CSR_CFG_READ             0x00010000
#define RT_BBP_CSR_CFG_BUSY             0x00020000
#define RT_BBP_CSR_CFG_RW_MODE          0x00080000

#define RT_RF_CSR_CFG0_VALUE            0x1fffffff
#define RT_RF_CSR_CFG0_BUSY             0x80000000

#define RT_H2M_MAILBOX_OWNER            0xff000000
#define RT_MCU_SLEEP                    0x30
#define RT_MCU_WAKEUP                   0x31
#define RT_MCU_LED_AG_CONF              0x52
#define RT_MCU_LED_ACT_CONF             0x53
#define RT_MCU_LED_POLARITY             0x54
#define RT_MCU_BOOT_SIGNAL              0x72

#define RT_PBF_SYS_CTRL_READY           0x00000080

#define RT_WPDMA_ENABLE_TX_DMA          0x00000001
#define RT_WPDMA_TX_DMA_BUSY            0x00000002
#define RT_WPDMA_ENABLE_RX_DMA          0x00000004
#define RT_WPDMA_RX_DMA_BUSY            0x00000008
#define RT_WPDMA_BURST_SIZE_MASK        0x00000030
#define RT_WPDMA_BURST_SIZE_SHIFT       4
#define RT_WPDMA_TX_WRITEBACK_DONE      0x00000040
#define RT_WPDMA_BIG_ENDIAN             0x00000080
#define RT_WPDMA_RX_HDR_SCATTER         0x0000ff00
#define RT_WPDMA_HDR_SEG_LEN            0xffff0000

#define RT_USB_DMA_AGG_TIMEOUT_MASK     0x000000ff
#define RT_USB_DMA_AGG_LIMIT_MASK       0x0000ff00
#define RT_USB_DMA_AGG_LIMIT_SHIFT      8
#define RT_USB_DMA_PHY_CLEAR            0x00010000
#define RT_USB_DMA_RX_AGG_EN            0x00200000
#define RT_USB_DMA_RX_BULK_EN           0x00400000
#define RT_USB_DMA_TX_BULK_EN           0x00800000

#define RT_MAC_STATUS_BBP_RF_BUSY       0x00000003

#define RT_BCN_TIME_INTERVAL_MASK       0x0000ffff
#define RT_BCN_TIME_TSF_TICKING         0x00010000
#define RT_BCN_TIME_TSF_SYNC_MASK       0x00060000
#define RT_BCN_TIME_TSF_SYNC_SHIFT      17
#define RT_BCN_TIME_TBTT_ENABLE         0x00080000
#define RT_BCN_TIME_BEACON_GEN          0x00100000
#define RT_BCN_TIME_COMPENSATE_MASK     0xf0000000

#define RT_RX_FILTER_DROP_CRC_ERROR     0x00000001
#define RT_RX_FILTER_DROP_PHY_ERROR     0x00000002
#define RT_RX_FILTER_DROP_NOT_TO_ME     0x00000004
#define RT_RX_FILTER_DROP_NOT_MY_BSSID  0x00000008
#define RT_RX_FILTER_DROP_VER_ERROR     0x00000010
#define RT_RX_FILTER_DROP_MULTICAST     0x00000020
#define RT_RX_FILTER_DROP_BROADCAST     0x00000040
#define RT_RX_FILTER_DROP_DUPLICATE     0x00000080
#define RT_RX_FILTER_DROP_CONTROL       0x0001ff00
#define RT_RX_FILTER_DROP_BA            0x00004000

#define RT_TX_PIN_PA_PE_A0_EN           0x00000001
#define RT_TX_PIN_PA_PE_G0_EN           0x00000002
#define RT_TX_PIN_PA_PE_A1_EN           0x00000004
#define RT_TX_PIN_PA_PE_G1_EN           0x00000008
#define RT_TX_PIN_LNA_PE_A0_EN          0x00000100
#define RT_TX_PIN_LNA_PE_G0_EN          0x00000200
#define RT_TX_PIN_LNA_PE_A1_EN          0x00000400
#define RT_TX_PIN_LNA_PE_G1_EN          0x00000800
#define RT_TX_PIN_RFTR_EN               0x00010000
#define RT_TX_PIN_TRSW_EN               0x00040000
#define RT_TX_PIN_PA_PE_A2_EN           0x01000000
#define RT_TX_PIN_PA_PE_G2_EN           0x02000000
#define RT_TX_PIN_LNA_PE_A2_EN          0x10000000
#define RT_TX_PIN_LNA_PE_G2_EN          0x20000000

#define RT_TX_BAND_HT40_MINUS           0x00000001
#define RT_TX_BAND_A                    0x00000002
#define RT_TX_BAND_BG                   0x00000004

#define RT_WCID_ATTR_KEYTAB             0x00000001
#define RT_WCID_ATTR_CIPHER_SHIFT       1
#define RT_WCID_ATTR_RX_WIUDF_SHIFT     7

#define RT_CIPHER_NONE                  0
#define RT_CIPHER_WEP64                 1
#define RT_CIPHER_WEP128                2
#define RT_CIPHER_TKIP                  3
#define RT_CIPHER_AES                   4

#define RT_EEPROM_MAC_ADDR              0x02
#define RT_EEPROM_NIC_CONF0             0x1a
#define RT_EEPROM_NIC_CONF1             0x1b
#define RT_EEPROM_FREQ                  0x1d
#define RT_EEPROM_LED_AG_CONF           0x1e
#define RT_EEPROM_LED_ACT_CONF          0x1f
#define RT_EEPROM_LED_POLARITY          0x20
#define RT_EEPROM_LNA                   0x22
#define RT_EEPROM_RSSI_BG               0x23
#define RT_EEPROM_RSSI_BG2              0x24
#define RT_EEPROM_RSSI_A                0x25
#define RT_EEPROM_RSSI_A2               0x26
#define RT_EEPROM_TXPOWER_BG1           0x29
#define RT_EEPROM_TXPOWER_BG2           0x30
#define RT_EEPROM_TXPOWER_A1            0x3c
#define RT_EEPROM_TXPOWER_A2            0x53
#define RT_EEPROM_TXPOWER_BYRATE        0x6f
#define RT_EEPROM_BBP_START             0x78
#define RT_EEPROM_BBP_SIZE              16

#define RT_RF2820                       0x0001
#define RT_RF2850                       0x0002
#define RT_RF2720                       0x0003
#define RT_RF2750                       0x0004

#define RT_REV_RT2860C                  0x0100
#define RT_REV_RT2860D                  0x0101

#define RT_TXINFO_SIZE                  4
#define RT_TXWI_SIZE                    16
#define RT_RXINFO_SIZE                  4
#define RT_RXWI_SIZE                    16
#define RT_RXD_SIZE                     4

#define RT_TXINFO_PKT_LEN_MASK          0x0000ffff
#define RT_TXINFO_WIV                   0x01000000
#define RT_TXINFO_QSEL_EDCA             0x04000000

#define RT_TXWI_W0_PHYMODE_SHIFT        30
#define RT_TXWI_W0_MCS_SHIFT            16
#define RT_TXWI_W0_TX_OP_SHIFT          8
#define RT_TXWI_W1_ACK                  0x00000001
#define RT_TXWI_W1_NSEQ                 0x00000002
#define RT_TXWI_W1_WCID_SHIFT           8
#define RT_TXWI_W1_LENGTH_SHIFT         16
#define RT_TXWI_W1_PACKETID_SHIFT       28

#define RT_RXINFO_PKT_LEN_MASK          0x0000ffff
#define RT_RXWI_W0_LENGTH_SHIFT         16
#define RT_RXWI_W0_LENGTH_MASK          0x00000fff
#define RT_RXWI_W1_MCS_SHIFT            16
#define RT_RXWI_W1_PHYMODE_SHIFT        30
#define RT_RXD_UNICAST_TO_ME            0x00000010
#define RT_RXD_CRC_ERROR                0x00000100
#define RT_RXD_CIPHER_ERROR_MASK        0x00000600
#define RT_RXD_L2PAD                    0x00004000
#define RT_RXD_DECRYPTED                0x00010000

#define RT_PHY_CCK                      0
#define RT_PHY_OFDM                     1

#define DOT11_FC0_TYPE_MASK             0x0c
#define DOT11_FC0_TYPE_MGMT             0x00
#define DOT11_FC0_TYPE_CTRL             0x04
#define DOT11_FC0_TYPE_DATA             0x08
#define DOT11_FC0_SUBTYPE_MASK          0xf0
#define DOT11_SUBTYPE_ASSOC_REQ         0x00
#define DOT11_SUBTYPE_ASSOC_RESP        0x10
#define DOT11_SUBTYPE_PROBE_REQ         0x40
#define DOT11_SUBTYPE_PROBE_RESP        0x50
#define DOT11_SUBTYPE_BEACON            0x80
#define DOT11_SUBTYPE_DISASSOC          0xa0
#define DOT11_SUBTYPE_AUTH              0xb0
#define DOT11_SUBTYPE_DEAUTH            0xc0
#define DOT11_SUBTYPE_ACTION            0xd0
#define DOT11_FC1_TODS                  0x01
#define DOT11_FC1_FROMDS                0x02
#define DOT11_FC1_PROTECTED             0x40
#define DOT11_HEADER_LENGTH             24
#define DOT11_QOS_SUBTYPE               0x80

#define DOT11_IE_SSID                   0
#define DOT11_IE_RATES                  1
#define DOT11_IE_DS_PARAMS              3
#define DOT11_IE_RSN                    48
#define DOT11_IE_TIMEOUT_INTERVAL       56
#define DOT11_IE_EXT_RATES              50
#define DOT11_IE_VENDOR                 221

typedef struct _RT_BSS
{
    UCHAR Bssid[RT_ADDRESS_LENGTH];
    UCHAR Ssid[DOT11_SSID_MAX_LENGTH];
    ULONG SsidLength;
    ULONG Channel;
    LONG Rssi;
    USHORT Capability;
    USHORT BeaconPeriod;
    ULONGLONG Timestamp;
    ULONGLONG LastSeen;
    ULONG IeLength;
    UCHAR Ies[RT_MAX_IE_BYTES];
} RT_BSS, *PRT_BSS;

typedef enum _RT_LINK_STATE
{
    RtLinkIdle,
    RtLinkAuthenticating,
    RtLinkAssociating,
    RtLinkAssociated
} RT_LINK_STATE;

typedef enum _RT_COMMAND
{
    RtCommandNone,
    RtCommandScan,
    RtCommandConnect,
    RtCommandDisconnect,
    RtCommandReset
} RT_COMMAND;

typedef struct _RT_ADAPTER RT_ADAPTER, *PRT_ADAPTER;

typedef struct _RT_URB_CONTEXT
{
    PRT_ADAPTER Adapter;
    PIRP Irp;
    URB Urb;
    PUCHAR Buffer;
    ULONG Index;
    PNET_BUFFER_LIST Nbl;
    BOOLEAN InUse;
} RT_URB_CONTEXT, *PRT_URB_CONTEXT;

struct _RT_ADAPTER
{
    NDIS_HANDLE MiniportAdapterHandle;
    PDEVICE_OBJECT PhysicalDeviceObject;
    PDEVICE_OBJECT LowerDeviceObject;
    PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor;
    USBD_CONFIGURATION_HANDLE ConfigurationHandle;
    USBD_PIPE_HANDLE BulkInPipe;
    USBD_PIPE_HANDLE BulkOutPipe;
    USHORT BulkOutPacketSize;

    KMUTEX RegisterLock;
    NDIS_SPIN_LOCK Lock;
    NDIS_SPIN_LOCK TxLock;

    ULONG ChipId;
    ULONG ChipRevision;
    USHORT RfType;
    UCHAR TxChains;
    UCHAR RxChains;
    UCHAR FrequencyOffset;
    CHAR LnaGainBg;
    CHAR LnaGainA;
    CHAR RssiOffsetBg[3];
    CHAR RssiOffsetA[3];
    BOOLEAN ExternalLnaBg;
    BOOLEAN ExternalLnaA;
    BOOLEAN DualBand;
    USHORT Eeprom[RT_EEPROM_WORDS];

    UCHAR PermanentAddress[RT_ADDRESS_LENGTH];
    UCHAR CurrentAddress[RT_ADDRESS_LENGTH];
    ULONG CurrentChannel;
    BOOLEAN HardwareReady;

    PETHREAD WorkerThread;
    KEVENT WorkerEvent;
    KEVENT WorkerStopped;
    BOOLEAN WorkerStop;
    RT_COMMAND PendingCommand;
    BOOLEAN ScanQueued;
    BOOLEAN ConnectQueued;
    BOOLEAN DisconnectQueued;
    DOT11_SSID ScanSsid;

    RT_BSS Bss[RT_MAX_BSS];
    ULONG BssCount;
    BOOLEAN Scanning;
    PNDIS_OID_REQUEST PendingScanOid;

    ULONG OperationMode;
    DOT11_PHY_TYPE CurrentPhyType;
    ULONG PacketFilter;
    BOOLEAN RadioOn;
    BOOLEAN AutoConfigEnabled;
    ULONG RtsThreshold;
    ULONG FragmentationThreshold;
    DOT11_SSID DesiredSsid;
    BOOLEAN HaveDesiredSsid;
    UCHAR DesiredBssid[RT_ADDRESS_LENGTH];
    BOOLEAN HaveDesiredBssid;
    DOT11_BSS_TYPE DesiredBssType;
    DOT11_AUTH_ALGORITHM AuthAlgorithm;
    DOT11_CIPHER_ALGORITHM UnicastCipher;
    DOT11_CIPHER_ALGORITHM MulticastCipher;

    RT_LINK_STATE LinkState;
    UCHAR ConnectedBssid[RT_ADDRESS_LENGTH];
    ULONG ConnectedChannel;
    USHORT AssociationId;
    USHORT SequenceNumber;
    LONG ConnectedRssi;
    ULONGLONG LastBeaconTime;
    KEVENT MlmeEvent;
    UCHAR MlmeExpect;
    USHORT MlmeStatus;
    USHORT MlmeAid;
    ULONG AssocRequestIeLength;
    UCHAR AssocRequestIes[RT_MAX_IE_BYTES];
    ULONG AssocResponseIeLength;
    UCHAR AssocResponseIes[RT_MAX_IE_BYTES];
    UCHAR SupportedRates[16];
    ULONG SupportedRateCount;
    BOOLEAN PairwiseKeyInstalled;
    BOOLEAN PmfActive;
    BOOLEAN SoftwareCrypto;
    BOOLEAN MgmtKeyValid;
    ULONGLONG DataRxPn;
    BOOLEAN GroupKeyValid[4];
    UCHAR GroupKey[4][16];
    ULONGLONG GroupRxPn[4];
    UCHAR MgmtTk[16];
    ULONGLONG MgmtTxPn;
    ULONGLONG MgmtRxPn;
    BOOLEAN SaQueryPending;
    USHORT SaQueryId;
    ULONGLONG SaQueryDeadline;
    BOOLEAN IgtkValid;
    USHORT IgtkKeyId;
    UCHAR Igtk[16];
    ULONGLONG IgtkRxIpn;

    NDIS_HANDLE RxNblPool;
    RT_URB_CONTEXT Rx[RT_RX_URBS];
    RT_URB_CONTEXT Tx[RT_TX_URBS];
    LONG RxPending;
    LONG TxPending;
    PNET_BUFFER_LIST TxQueueHead;
    PNET_BUFFER_LIST TxQueueTail;
    BOOLEAN Halting;
    BOOLEAN Paused;
    BOOLEAN DataPathRunning;
    LONG RxIndicated;
    BOOLEAN HoldRx;
    PNET_BUFFER_LIST HeldRxHead;
    PNET_BUFFER_LIST HeldRxTail;
    ULONG HeldRxCount;

    ULONG64 TxOk;
    ULONG64 TxError;
    ULONG64 TxBytes;
    ULONG64 RxOk;
    ULONG64 RxError;
    ULONG64 RxBytes;
};

PVOID RtAllocate(_In_ SIZE_T Size);
VOID RtFree(_In_opt_ PVOID Buffer);
VOID RtSleep(_In_ ULONG Milliseconds);
VOID RtWakeWorker(_In_ PRT_ADAPTER Adapter);

NTSTATUS RtUsbInitialize(_In_ PRT_ADAPTER Adapter);
VOID RtUsbCleanup(_In_ PRT_ADAPTER Adapter);
NTSTATUS RtVendorRequest(_In_ PRT_ADAPTER Adapter, _In_ UCHAR Request, _In_ BOOLEAN In, _In_ USHORT Value, _In_ USHORT Index, _Inout_updates_bytes_opt_(Length) PVOID Buffer, _In_ USHORT Length);
ULONG RtRead(_In_ PRT_ADAPTER Adapter, _In_ USHORT Offset);
VOID RtWrite(_In_ PRT_ADAPTER Adapter, _In_ USHORT Offset, _In_ ULONG Value);
VOID RtWriteBlock(_In_ PRT_ADAPTER Adapter, _In_ USHORT Offset, _In_reads_bytes_(Length) const VOID* Buffer, _In_ ULONG Length);
NTSTATUS RtStartReceive(_In_ PRT_ADAPTER Adapter);
VOID RtStopIo(_In_ PRT_ADAPTER Adapter);
NTSTATUS RtSendFrame(_In_ PRT_ADAPTER Adapter, _In_reads_bytes_(Length) const UCHAR* Frame, _In_ ULONG Length, _In_ BOOLEAN Management, _In_opt_ PNET_BUFFER_LIST Nbl);
ULONG RtFreeTxCount(_In_ PRT_ADAPTER Adapter);
VOID RtTxPump(_In_ PRT_ADAPTER Adapter, _In_ BOOLEAN AtDispatch);
MINIPORT_RETURN_NET_BUFFER_LISTS RtMiniportReturnNetBufferLists;
NTSTATUS RtSendNetBuffer(_In_ PRT_ADAPTER Adapter, _In_ PNET_BUFFER NetBuffer, _In_ BOOLEAN Exempt, _In_opt_ PNET_BUFFER_LIST Nbl);

NTSTATUS RtHardwareInitialize(_In_ PRT_ADAPTER Adapter);
VOID RtHardwareShutdown(_In_ PRT_ADAPTER Adapter);
VOID RtSetChannel(_In_ PRT_ADAPTER Adapter, _In_ ULONG Channel);
VOID RtSetBssid(_In_ PRT_ADAPTER Adapter, _In_reads_bytes_opt_(RT_ADDRESS_LENGTH) const UCHAR* Bssid);
VOID RtSetRxFilter(_In_ PRT_ADAPTER Adapter, _In_ BOOLEAN Scanning);
VOID RtSetAssociated(_In_ PRT_ADAPTER Adapter, _In_ BOOLEAN Associated, _In_ USHORT BeaconInterval);
NTSTATUS RtInstallKey(_In_ PRT_ADAPTER Adapter, _In_ BOOLEAN Pairwise, _In_ ULONG KeyIndex, _In_ ULONG Cipher, _In_reads_bytes_(KeyLength) const UCHAR* Key, _In_ ULONG KeyLength, _In_reads_bytes_opt_(RT_ADDRESS_LENGTH) const UCHAR* Peer);
VOID RtRemoveKeys(_In_ PRT_ADAPTER Adapter);
LONG RtRssiFromRxwi(_In_ PRT_ADAPTER Adapter, _In_ ULONG Rxwi2, _In_ ULONG Channel);

VOID RtMlmeReceive(_In_ PRT_ADAPTER Adapter, _In_reads_bytes_(Length) const UCHAR* Frame, _In_ ULONG Length, _In_ LONG Rssi, _In_ BOOLEAN HwDecrypted);
KSTART_ROUTINE RtWorkerThread;
VOID RtIndicateLinkState(_In_ PRT_ADAPTER Adapter, _In_ BOOLEAN Connected);
VOID RtIndicateDot11Status(_In_ PRT_ADAPTER Adapter, _In_ NDIS_STATUS StatusCode, _In_reads_bytes_opt_(BufferSize) PVOID Buffer, _In_ ULONG BufferSize);
ULONG RtBuildBssList(_In_ PRT_ADAPTER Adapter, _Out_writes_bytes_opt_(BufferLength) PUCHAR Buffer, _In_ ULONG BufferLength, _Out_ PULONG BytesNeeded);
ULONG RtChannelToFrequency(_In_ ULONG Channel);

VOID RtReceiveData(_In_ PRT_ADAPTER Adapter, _In_reads_bytes_(Length) const UCHAR* Frame, _In_ ULONG Length, _In_ LONG Rssi, _In_ ULONG Rate);

MINIPORT_OID_REQUEST RtMiniportOidRequest;
MINIPORT_CANCEL_OID_REQUEST RtMiniportCancelOidRequest;
extern NDIS_OID RtSupportedOids[];
extern ULONG RtSupportedOidCount;
