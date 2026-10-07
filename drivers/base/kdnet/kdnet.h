/*
 * PROJECT:     LiberNT Network Kernel Debugger Transport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Internal interfaces of the network transport
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define NOEXTAPI
#include <ntifs.h>
#include <halfuncs.h>
#include <stdio.h>
#include <arc/arc.h>
#include <windbgkd.h>
#include <kddll.h>
#include <pstypes.h>
#include <rtlfuncs.h>
#include <kdnetextensibility.h>
#include <kdterm.h>

#define KDNET_TAG               'tNdK'

#define KDNET_DEFAULT_PORT      50000
#define KDNET_DATAGRAM_SIZE     1200
#define KDNET_OUTPUT_SIZE       0x40000
#define KDNET_INPUT_SIZE        256
#define KDNET_KEY_TEXT_SIZE     96
#define KDNET_HOST_NAME_SIZE    128

#define KDNET_SECOND            1000000ULL
#define KDNET_MILLISECOND       1000ULL

#define KDNET_IP(a, b, c, d)    (((ULONG)(a) << 24) | ((ULONG)(b) << 16) | ((ULONG)(c) << 8) | (ULONG)(d))
#define KDNET_IP_BROADCAST      0xFFFFFFFF

typedef struct _KDNET_OPTIONS
{
    ULONG HostIp;
    USHORT HostPort;
    BOOLEAN HaveLocation;
    ULONG Bus;
    ULONG Device;
    ULONG Function;
    BOOLEAN HaveKey;
    CHAR Key[KDNET_KEY_TEXT_SIZE];
    CHAR HostName[KDNET_HOST_NAME_SIZE];
} KDNET_OPTIONS, *PKDNET_OPTIONS;

extern KDNET_OPTIONS KdNetOptions;
extern volatile BOOLEAN KdNetReady;
extern KD_TERMINAL_HOST KdNetHost;
extern BOOLEAN KdNetStopped;

ULONG64
KdNetTime(VOID);

BOOLEAN
KdNetArchRandom(
    _Out_ PULONG Value);

BOOLEAN
KdNetArchGetDeviceTree(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _Out_ const VOID **Blob,
    _Out_ PULONG Size);

#define KDNET_MODULE_NAME_SIZE  64
#define KDNET_PATH_SIZE         64

typedef struct _KDNET_PLATFORM_DEVICE
{
    WCHAR ModuleName[KDNET_MODULE_NAME_SIZE];
    KD_NAMESPACE_ENUM NameSpace;
    USHORT PortType;
    USHORT PortSubtype;
    PHYSICAL_ADDRESS Base;
    ULONG Length;
    PVOID OemData;
    ULONG OemDataLength;
    CHAR Path[KDNET_PATH_SIZE];
} KDNET_PLATFORM_DEVICE, *PKDNET_PLATFORM_DEVICE;

typedef BOOLEAN
(*PKDNET_PLATFORM_CALLBACK)(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PKDNET_PLATFORM_DEVICE Device);

BOOLEAN
KdPlatformEnumerate(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PKDNET_PLATFORM_CALLBACK Callback);

VOID
KdNetService(VOID);

FORCEINLINE
USHORT
KdNetGet16(
    _In_reads_(2) const UCHAR *Data)
{
    return (USHORT)((Data[0] << 8) | Data[1]);
}

FORCEINLINE
ULONG
KdNetGet32(
    _In_reads_(4) const UCHAR *Data)
{
    return ((ULONG)Data[0] << 24) | ((ULONG)Data[1] << 16) | ((ULONG)Data[2] << 8) | Data[3];
}

FORCEINLINE
VOID
KdNetPut16(
    _Out_writes_(2) PUCHAR Data,
    _In_ USHORT Value)
{
    Data[0] = (UCHAR)(Value >> 8);
    Data[1] = (UCHAR)Value;
}

FORCEINLINE
VOID
KdNetPut32(
    _Out_writes_(4) PUCHAR Data,
    _In_ ULONG Value)
{
    Data[0] = (UCHAR)(Value >> 24);
    Data[1] = (UCHAR)(Value >> 16);
    Data[2] = (UCHAR)(Value >> 8);
    Data[3] = (UCHAR)Value;
}

FORCEINLINE
ULONG
KdNetGetLe32(
    _In_reads_(4) const UCHAR *Data)
{
    return Data[0] | ((ULONG)Data[1] << 8) | ((ULONG)Data[2] << 16) | ((ULONG)Data[3] << 24);
}

FORCEINLINE
ULONG64
KdNetGetLe64(
    _In_reads_(8) const UCHAR *Data)
{
    return KdNetGetLe32(Data) | ((ULONG64)KdNetGetLe32(Data + 4) << 32);
}

FORCEINLINE
VOID
KdNetPutLe16(
    _Out_writes_(2) PUCHAR Data,
    _In_ USHORT Value)
{
    Data[0] = (UCHAR)Value;
    Data[1] = (UCHAR)(Value >> 8);
}

FORCEINLINE
USHORT
KdNetGetLe16(
    _In_reads_(2) const UCHAR *Data)
{
    return (USHORT)(Data[0] | (Data[1] << 8));
}

FORCEINLINE
VOID
KdNetPutLe32(
    _Out_writes_(4) PUCHAR Data,
    _In_ ULONG Value)
{
    Data[0] = (UCHAR)Value;
    Data[1] = (UCHAR)(Value >> 8);
    Data[2] = (UCHAR)(Value >> 16);
    Data[3] = (UCHAR)(Value >> 24);
}

FORCEINLINE
VOID
KdNetPutLe64(
    _Out_writes_(8) PUCHAR Data,
    _In_ ULONG64 Value)
{
    KdNetPutLe32(Data, (ULONG)Value);
    KdNetPutLe32(Data + 4, (ULONG)(Value >> 32));
}

extern UCHAR KdNicAddress[MAC_ADDRESS_SIZE];
extern UCHAR KdNicLinkState;
extern DEBUG_DEVICE_DESCRIPTOR KdNicDevice;

NTSTATUS
KdNicInitialize(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

PUCHAR
KdNicGetTxBuffer(
    _Out_ PULONG Handle);

PUCHAR
KdNicGetTxBufferAddress(
    _In_ ULONG Handle);

VOID
KdNicSend(
    _In_ ULONG Handle,
    _In_ ULONG Length);

BOOLEAN
KdNicReceive(
    _Out_ PUCHAR *Frame,
    _Out_ PULONG Length,
    _Out_ PULONG Handle);

VOID
KdNicRelease(
    _In_ ULONG Handle);

#define KDNET_UDP_HEADERS       42

extern ULONG KdIpAddress;
extern ULONG KdIpGateway;
extern ULONG KdIpMask;

VOID
KdIpInitialize(VOID);

VOID
KdIpInput(
    _In_reads_bytes_(Length) PUCHAR Frame,
    _In_ ULONG Length);

VOID
KdIpTimer(
    _In_ ULONG64 Now);

PUCHAR
KdUdpBegin(
    _In_ ULONG DestinationIp,
    _Out_ PULONG Handle);

VOID
KdUdpEnd(
    _In_ ULONG Handle,
    _In_ ULONG DestinationIp,
    _In_ USHORT DestinationPort,
    _In_ USHORT SourcePort,
    _In_ ULONG Length);

#define KDNET_HASH_SIZE         32
#define KDNET_KEY_SIZE          32
#define KDNET_TAG_SIZE          16
#define KDNET_DH_SIZE           32

NTSTATUS
KdCryptoInitialize(VOID);

VOID
KdCryptoAddEntropy(
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length);

VOID
KdCryptoRandom(
    _Out_writes_bytes_(Length) PUCHAR Data,
    _In_ ULONG Length);

VOID
KdCryptoHash(
    _In_reads_bytes_opt_(Length1) const UCHAR *Data1,
    _In_ ULONG Length1,
    _In_reads_bytes_opt_(Length2) const UCHAR *Data2,
    _In_ ULONG Length2,
    _Out_writes_(KDNET_HASH_SIZE) PUCHAR Hash);

VOID
KdCryptoHmac(
    _In_reads_bytes_(KeyLength) const UCHAR *Key,
    _In_ ULONG KeyLength,
    _In_reads_bytes_opt_(Length1) const UCHAR *Data1,
    _In_ ULONG Length1,
    _In_reads_bytes_opt_(Length2) const UCHAR *Data2,
    _In_ ULONG Length2,
    _Out_writes_(KDNET_HASH_SIZE) PUCHAR Mac);

VOID
KdCryptoHkdf(
    _In_reads_(KDNET_HASH_SIZE) const UCHAR *ChainingKey,
    _In_reads_bytes_opt_(Length) const UCHAR *Input,
    _In_ ULONG Length,
    _Out_writes_(KDNET_HASH_SIZE) PUCHAR Output1,
    _Out_writes_(KDNET_HASH_SIZE) PUCHAR Output2,
    _Out_writes_opt_(KDNET_HASH_SIZE) PUCHAR Output3);

VOID
KdCryptoSeal(
    _In_reads_(KDNET_KEY_SIZE) const UCHAR *Key,
    _In_ ULONG64 Counter,
    _In_reads_bytes_opt_(AssociatedLength) const UCHAR *Associated,
    _In_ ULONG AssociatedLength,
    _In_reads_bytes_(Length) const UCHAR *Plain,
    _In_ ULONG Length,
    _Out_writes_bytes_(Length + KDNET_TAG_SIZE) PUCHAR Cipher);

BOOLEAN
KdCryptoOpen(
    _In_reads_(KDNET_KEY_SIZE) const UCHAR *Key,
    _In_ ULONG64 Counter,
    _In_reads_bytes_opt_(AssociatedLength) const UCHAR *Associated,
    _In_ ULONG AssociatedLength,
    _In_reads_bytes_(Length) const UCHAR *Cipher,
    _In_ ULONG Length,
    _Out_writes_bytes_(Length - KDNET_TAG_SIZE) PUCHAR Plain);

BOOLEAN
KdCryptoExchange(
    _In_reads_(KDNET_DH_SIZE) const UCHAR *PeerPublic,
    _Out_writes_(KDNET_DH_SIZE) PUCHAR Public,
    _Out_writes_(KDNET_DH_SIZE) PUCHAR Secret);

BOOLEAN
KdCryptoEqual(
    _In_reads_bytes_(Length) const UCHAR *Left,
    _In_reads_bytes_(Length) const UCHAR *Right,
    _In_ ULONG Length);

VOID
KdCryptoWipe(
    _Out_writes_bytes_(Length) PVOID Data,
    _In_ ULONG Length);

extern CHAR KdSessionKeyText[KDNET_KEY_TEXT_SIZE];

VOID
KdSessionInitialize(VOID);

VOID
KdSessionInput(
    _In_ ULONG SourceIp,
    _In_ USHORT SourcePort,
    _In_reads_bytes_(Length) PUCHAR Data,
    _In_ ULONG Length);

VOID
KdSessionTimer(
    _In_ ULONG64 Now);

VOID
KdSessionWrite(
    _In_reads_bytes_(Length) const CHAR *Buffer,
    _In_ ULONG Length);

BOOLEAN
KdSessionRead(
    _Out_ PUCHAR Byte);

BOOLEAN
KdSessionTakeBreak(VOID);

BOOLEAN
KdSessionAttached(
    _Out_opt_ PULONG Columns,
    _Out_opt_ PULONG Rows);
