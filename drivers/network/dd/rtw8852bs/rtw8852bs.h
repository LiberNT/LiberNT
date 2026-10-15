/*
 * PROJECT:     LiberNT Realtek RTL8852BS Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver definitions for the RTL8852BS SDIO wireless adapter
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <ndis.h>
#include <windot11.h>
#include <ntddsd.h>

#include "reg.h"

#define RTW_TAG                         'B88R'

#define RTW_ADDRESS_LENGTH              6
#define RTW_SDIO_FUNCTION               1
#define RTW_SDIO_BLOCK_SIZE             512
#define RTW_SDIO_BYTE_MODE_MAX          512
#define RTW_SDIO_MAX_ADDRESS            0x1FFFF
#define RTW_SDIO_LOCAL_START            0x1000
#define RTW_SDIO_LOCAL_END              0x1F00
#define RTW_SDIO_INDIRECT_POLLS         50
#define RTW_SDIO_DEAD_RETRIES           10
#define RTW_SCRATCH_SIZE                64

#define RTW_EFUSE_PHYSICAL_SIZE         1216
#define RTW_EFUSE_LOGICAL_SIZE          2048
#define RTW_EFUSE_SEC_CTRL_SIZE         4
#define RTW_EFUSE_SDIO_MAC_OFFSET       0x41A
#define RTW_EFUSE_PWR_K_OFFSET          0x5E9
#define RTW_EFUSE_PWR_K_VALUE           0xAA
#define RTW_EFUSE_SECURE_OFFSET         0x5EC
#define RTW_EFUSE_PHYCAP_OFFSET         0x580
#define RTW_EFUSE_PHYCAP_SIZE           128

#define RTW_FIRMWARE_PATH               L"\\SystemRoot\\System32\\drivers\\realtek\\rtw8852b_fw-2.bin"
#define RTW_FIRMWARE_MAX_SIZE           (4 * 1024 * 1024)
#define RTW_FW_TYPE_NORMAL              1
#define RTW_FW_TYPE_NORMAL_CE           5
#define RTW_FW_SECTION_MAX              8
#define RTW_FWDL_PART_SIZE              2020
#define RTW_TX_BUFFER_SIZE              8192
#define RTW_WD_BODY_LENGTH              24
#define RTW_H2C_HEADER_LENGTH           8
#define RTW_DMA_CH_H2C                  12
#define RTW_RX_BUFFER_SIZE              (32 * 1024)
#define RTW_H2C_CAT_MAC                 1
#define RTW_H2C_CL_FW_INFO              0
#define RTW_H2C_FUNC_LOG_CFG            0

#define RTW_MTU_SIZE                    1500
#define RTW_MAX_80211_FRAME             2346
#define RTW_MAX_BSS                     48
#define RTW_MAX_IE_BYTES                768
#define RTW_HELD_RX_MAX                 8
#define RTW_TX_QUEUE_DEPTH              16
#define RTW_RX_QUEUE_DEPTH              8
#define RTW_LINK_SPEED_BPS              54000000ULL
#define RTW_SCAN_DWELL_MS               120
#define RTW_AUTH_TIMEOUT_MS             300
#define RTW_ASSOC_TIMEOUT_MS            300
#define RTW_MLME_RETRIES                3
#define RTW_ASSOC_COMEBACK_MAX          8
#define RTW_JOIN_ATTEMPTS               2
#define RTW_SA_QUERY_MAX_TIMEOUT_MS     1024
#define RTW_BEACON_LOSS_MS              8000
#define RTW_DEFAULT_RSSI                (-60)

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
#define DOT11_FCS_LENGTH                4

#define DOT11_IE_SSID                   0
#define DOT11_IE_RATES                  1
#define DOT11_IE_DS_PARAMS              3
#define DOT11_IE_RSN                    48
#define DOT11_IE_TIMEOUT_INTERVAL       56
#define DOT11_IE_EXT_RATES              50
#define RTW_RATE_MASK_CCK               0x00F
#define RTW_RATE_MASK_OFDM              0xFF0
#define DOT11_IE_VENDOR                 221

#define RTW_BB_GAIN_BANDS               8
#define RTW_LNA_GAIN_NUM                7
#define RTW_TIA_GAIN_NUM                2
#define RTW_RXSC_NUM_40                 9
#define RTW_RXSC_NUM_80                 13
#define RTW_SUBBAND_2G                  0
#define RTW_SUBBAND_5G_1                1
#define RTW_SUBBAND_5G_3                3
#define RTW_SUBBAND_5G_4                4

typedef struct _RTW_BB_GAIN
{
    CHAR LnaGain[RTW_BB_GAIN_BANDS][2][RTW_LNA_GAIN_NUM];
    CHAR TiaGain[RTW_BB_GAIN_BANDS][2][RTW_TIA_GAIN_NUM];
    CHAR LnaGainBypass[RTW_BB_GAIN_BANDS][2][RTW_LNA_GAIN_NUM];
    CHAR LnaOp1db[RTW_BB_GAIN_BANDS][2][RTW_LNA_GAIN_NUM];
    CHAR TiaLnaOp1db[RTW_BB_GAIN_BANDS][2][RTW_LNA_GAIN_NUM + 1];
    CHAR RplOfst20[RTW_BB_GAIN_BANDS][2];
    CHAR RplOfst40[RTW_BB_GAIN_BANDS][2][RTW_RXSC_NUM_40];
    CHAR RplOfst80[RTW_BB_GAIN_BANDS][2][RTW_RXSC_NUM_80];
} RTW_BB_GAIN, *PRTW_BB_GAIN;

typedef struct _RTW_BSS
{
    UCHAR Bssid[RTW_ADDRESS_LENGTH];
    UCHAR Ssid[DOT11_SSID_MAX_LENGTH];
    ULONG SsidLength;
    ULONG Channel;
    LONG Rssi;
    USHORT Capability;
    USHORT BeaconPeriod;
    ULONGLONG Timestamp;
    ULONGLONG LastSeen;
    ULONG IeLength;
    UCHAR Ies[RTW_MAX_IE_BYTES];
} RTW_BSS, *PRTW_BSS;

typedef enum _RTW_LINK_STATE
{
    RtwLinkIdle,
    RtwLinkAuthenticating,
    RtwLinkAssociating,
    RtwLinkAssociated
} RTW_LINK_STATE;

typedef enum _RTW_COMMAND
{
    RtwCommandNone,
    RtwCommandScan,
    RtwCommandConnect,
    RtwCommandDisconnect,
    RtwCommandReset
} RTW_COMMAND;

typedef struct _RTW_ADAPTER
{
    NDIS_HANDLE MiniportAdapterHandle;
    PDEVICE_OBJECT PhysicalDeviceObject;
    PDEVICE_OBJECT LowerDeviceObject;

    SDBUS_INTERFACE_STANDARD SdBus;
    BOOLEAN SdBusOpened;
    KMUTEX IoLock;
    PUCHAR Scratch;
    PMDL ScratchMdl;
    BOOLEAN PowerOn;
    BOOLEAN IoError;

    UCHAR ChipVersion;
    UCHAR AnalogVersion;
    BOOLEAN EfuseValid;
    BOOLEAN PowerCalibrated;
    PUCHAR LogicalEfuse;
    UCHAR PhyCap[RTW_EFUSE_PHYCAP_SIZE];

    BOOLEAN SecureBoot;
    UCHAR SecureInfo[2];

    PUCHAR Firmware;
    ULONG FirmwareLength;
    PUCHAR TxBuffer;
    PMDL TxMdl;
    FAST_MUTEX TxLock;
    UCHAR H2cSequence;

    PUCHAR RxBuffer;
    PMDL RxMdl;
    PKTHREAD WorkerThread;
    KEVENT WorkerEvent;
    KEVENT WorkerStopped;
    volatile BOOLEAN WorkerStop;
    volatile LONG InterruptPending;
    BOOLEAN InterruptRegistered;
    ULONG RxFilter;
    ULONG C2hCount;

    UCHAR RfeType;
    UCHAR XtalCap;
    BOOLEAN EfuseGainValid;
    CHAR EfuseGainOffset[2][5];
    CHAR GainOffsetBase;
    CHAR RssiBase;
    UCHAR CurrentChannel;
    RTW_BB_GAIN BbGain;

    UCHAR PermanentAddress[RTW_ADDRESS_LENGTH];
    UCHAR CurrentAddress[RTW_ADDRESS_LENGTH];
    UCHAR Bssid[RTW_ADDRESS_LENGTH];
    UCHAR NetType;
    USHORT Aid;

    NDIS_SPIN_LOCK Lock;
    NDIS_SPIN_LOCK TxQueueLock;
    BOOLEAN HardwareReady;
    PKTHREAD CommandThread;
    KEVENT CommandEvent;
    KEVENT CommandStopped;
    volatile BOOLEAN CommandStop;
    BOOLEAN ScanQueued;
    BOOLEAN ConnectQueued;
    BOOLEAN DisconnectQueued;
    DOT11_SSID ScanSsid;

    RTW_BSS Bss[RTW_MAX_BSS];
    ULONG BssCount;
    BOOLEAN Scanning;

    ULONG OperationMode;
    DOT11_PHY_TYPE CurrentPhyType;
    ULONG PacketFilter;
    BOOLEAN RadioOn;
    BOOLEAN AutoConfigEnabled;
    ULONG RtsThreshold;
    ULONG FragmentationThreshold;
    DOT11_SSID DesiredSsid;
    BOOLEAN HaveDesiredSsid;
    UCHAR DesiredBssid[RTW_ADDRESS_LENGTH];
    BOOLEAN HaveDesiredBssid;
    DOT11_BSS_TYPE DesiredBssType;
    DOT11_AUTH_ALGORITHM AuthAlgorithm;
    DOT11_CIPHER_ALGORITHM UnicastCipher;
    DOT11_CIPHER_ALGORITHM MulticastCipher;

    RTW_LINK_STATE LinkState;
    UCHAR ConnectedBssid[RTW_ADDRESS_LENGTH];
    ULONG ConnectedChannel;
    USHORT AssociationId;
    LONG ConnectedRssi;
    UCHAR RxPpduTransmitter[8][RTW_ADDRESS_LENGTH];
    UCHAR RxPpduValid;
    ULONG TxRateMask;
    UCHAR TxLowestRate;
    ULONGLONG LastBeaconTime;
    KEVENT MlmeEvent;
    UCHAR MlmeExpect;
    USHORT MlmeStatus;
    USHORT MlmeAid;
    ULONG AssocRequestIeLength;
    UCHAR AssocRequestIes[RTW_MAX_IE_BYTES];
    ULONG AssocResponseIeLength;
    UCHAR AssocResponseIes[RTW_MAX_IE_BYTES];
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
    PNET_BUFFER_LIST TxQueueHead;
    PNET_BUFFER_LIST TxQueueTail;
    volatile BOOLEAN Halting;
    volatile BOOLEAN DataPathRunning;
    volatile LONG RxIndicated;
    volatile LONG TxPending;
    BOOLEAN HoldRx;
    PNET_BUFFER_LIST HeldRxHead;
    PNET_BUFFER_LIST HeldRxTail;
    ULONG HeldRxCount;
    UCHAR TxFrame[RTW_MAX_80211_FRAME + 32];

    ULONG64 TxOk;
    ULONG64 TxError;
    ULONG64 TxBytes;
    ULONG64 RxOk;
    ULONG64 RxError;
    ULONG64 RxBytes;
} RTW_ADAPTER, *PRTW_ADAPTER;

extern NDIS_OID RtwSupportedOids[];
extern ULONG RtwSupportedOidCount;

FORCEINLINE
ULONG
RtwLe32(
    _In_reads_bytes_(4) const UCHAR *Data)
{
    return Data[0] | ((ULONG)Data[1] << 8) | ((ULONG)Data[2] << 16) | ((ULONG)Data[3] << 24);
}

FORCEINLINE
VOID
RtwPutLe32(
    _Out_writes_bytes_(4) PUCHAR Data,
    _In_ ULONG Value)
{
    Data[0] = (UCHAR)Value;
    Data[1] = (UCHAR)(Value >> 8);
    Data[2] = (UCHAR)(Value >> 16);
    Data[3] = (UCHAR)(Value >> 24);
}

PVOID
RtwAllocate(
    _In_ SIZE_T Size);

VOID
RtwFree(
    _In_opt_ PVOID Buffer);

VOID
RtwSleep(
    _In_ ULONG Milliseconds);

VOID
RtwStall(
    _In_ ULONG Microseconds);

NTSTATUS
RtwSdioTransfer(
    _In_ PRTW_ADAPTER Adapter,
    _In_ BOOLEAN Write,
    _In_ ULONG Address,
    _In_ PMDL Mdl,
    _In_ ULONG Length);

NTSTATUS
RtwSdioWritePacket(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Channel,
    _In_ ULONG Length);

NTSTATUS
RtwSdioOpen(
    _In_ PRTW_ADAPTER Adapter);

VOID
RtwSdioClose(
    _In_ PRTW_ADAPTER Adapter);

UCHAR
RtwRead8(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address);

USHORT
RtwRead16(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address);

ULONG
RtwRead32(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address);

VOID
RtwWrite8(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ UCHAR Value);

VOID
RtwWrite16(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ USHORT Value);

VOID
RtwWrite32(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Value);

VOID
RtwWrite32Set(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Bits);

VOID
RtwWrite32Clear(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Bits);

VOID
RtwWrite32Mask(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Mask,
    _In_ ULONG Value);

NTSTATUS
RtwPoll32(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Mask,
    _In_ ULONG Value,
    _In_ ULONG TimeoutUs);

NTSTATUS
RtwWriteXtalSi(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Offset,
    _In_ UCHAR Value,
    _In_ UCHAR Mask);

NTSTATUS
RtwReadXtalSi(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Offset,
    _Out_ PUCHAR Value);

NTSTATUS
RtwPowerOn(
    _In_ PRTW_ADAPTER Adapter);

VOID
RtwPowerOff(
    _In_ PRTW_ADAPTER Adapter);

NTSTATUS
RtwReadEfuse(
    _In_ PRTW_ADAPTER Adapter);

NTSTATUS
RtwMacPreInit(
    _In_ PRTW_ADAPTER Adapter);

NTSTATUS
RtwLoadFirmware(
    _In_ PRTW_ADAPTER Adapter);

NTSTATUS
RtwDownloadFirmware(
    _In_ PRTW_ADAPTER Adapter);

NTSTATUS
RtwMacInit(
    _In_ PRTW_ADAPTER Adapter);

NTSTATUS
RtwSendH2c(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Category,
    _In_ UCHAR Class,
    _In_ UCHAR Function,
    _In_ BOOLEAN DoneAck,
    _In_reads_bytes_opt_(Length) const VOID *Data,
    _In_ ULONG Length);

NTSTATUS
RtwStartRx(
    _In_ PRTW_ADAPTER Adapter);

VOID
RtwStopRx(
    _In_ PRTW_ADAPTER Adapter);

ULONG
RtwBbRead32(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Mask);

VOID
RtwBbWrite32(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Mask,
    _In_ ULONG Value);

ULONG
RtwRfRead(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Path,
    _In_ ULONG Address,
    _In_ ULONG Mask);

VOID
RtwRfWrite(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Path,
    _In_ ULONG Address,
    _In_ ULONG Mask,
    _In_ ULONG Value);

NTSTATUS
RtwPhyInit(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Channel);

VOID
RtwSetChannel(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Channel);

VOID
RtwRfkInit(
    _In_ PRTW_ADAPTER Adapter);

VOID
RtwRxDck(
    _In_ PRTW_ADAPTER Adapter);

VOID
RtwSetRxFilter(
    _In_ PRTW_ADAPTER Adapter,
    _In_ BOOLEAN AllBeacons);

NTSTATUS
RtwHardwareProbe(
    _In_ PRTW_ADAPTER Adapter);

const UCHAR *
RtwFindTxPowerElement(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Id,
    _Out_ PULONG EntrySize,
    _Out_ PULONG Count);

VOID
RtwSetTxPower(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Channel);

VOID
RtwPowerTrim(
    _In_ PRTW_ADAPTER Adapter);

NTSTATUS
RtwTxFrame(
    _In_ PRTW_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR *Frame,
    _In_ ULONG Length,
    _In_ BOOLEAN Management);

NTSTATUS
RtwVifInit(
    _In_ PRTW_ADAPTER Adapter);

VOID
RtwWakeCommand(
    _In_ PRTW_ADAPTER Adapter);

VOID
RtwTxPump(
    _In_ PRTW_ADAPTER Adapter);

NTSTATUS
RtwSendNetBuffer(
    _In_ PRTW_ADAPTER Adapter,
    _In_ PNET_BUFFER NetBuffer,
    _In_ BOOLEAN Exempt);

ULONG
RtwFrameHeaderLength(
    _In_reads_bytes_(Length) const UCHAR *Frame,
    _In_ ULONG Length);

VOID
RtwClearKeys(
    _In_ PRTW_ADAPTER Adapter);

VOID
RtwMlmeUpdateRssi(
    _In_ PRTW_ADAPTER Adapter,
    _In_reads_bytes_(RTW_ADDRESS_LENGTH) const UCHAR* Transmitter,
    _In_ LONG Rssi);

VOID
RtwMlmeReceive(
    _In_ PRTW_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR *Frame,
    _In_ ULONG Length,
    _In_ LONG Rssi,
    _In_ BOOLEAN HwDecrypted);

VOID
RtwReceiveData(
    _In_ PRTW_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR *Frame,
    _In_ ULONG Length,
    _In_ LONG Rssi,
    _In_ ULONG Rate);

VOID
RtwIndicateLinkState(
    _In_ PRTW_ADAPTER Adapter,
    _In_ BOOLEAN Connected);

VOID
RtwIndicateDot11Status(
    _In_ PRTW_ADAPTER Adapter,
    _In_ NDIS_STATUS StatusCode,
    _In_reads_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize);

ULONG
RtwBuildBssList(
    _In_ PRTW_ADAPTER Adapter,
    _Out_writes_bytes_opt_(BufferLength) PUCHAR Buffer,
    _In_ ULONG BufferLength,
    _Out_ PULONG BytesNeeded);

ULONG
RtwChannelToFrequency(
    _In_ ULONG Channel);

VOID
NTAPI
RtwCommandThread(
    _In_ PVOID Context);

NDIS_STATUS
NTAPI
RtwMiniportOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_OID_REQUEST OidRequest);

VOID
NTAPI
RtwMiniportCancelOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID RequestId);

VOID
NTAPI
RtwMiniportReturnNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags);

NTSTATUS
RtwVifSetLink(
    _In_ PRTW_ADAPTER Adapter,
    _In_reads_bytes_opt_(RTW_ADDRESS_LENGTH) const UCHAR *Bssid,
    _In_ BOOLEAN Associated,
    _In_ USHORT Aid,
    _In_ USHORT BeaconInterval);

NTSTATUS
RtwVifSetRates(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG RateMask);
