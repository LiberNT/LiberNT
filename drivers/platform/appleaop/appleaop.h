/*
 * PROJECT:     LiberNT Apple AOP Driver
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shared state, AFK and EPIC definitions of the Apple Always-On Processor driver
 * COPYRIGHT:   Copyright (C) The Asahi Linux Contributors
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntifs.h>
#include <reactos/drivers/fdtdrv.h>
#include <reactos/drivers/applertkit.h>
#include <reactos/drivers/appledart.h>
#include <reactos/drivers/appleaop.h>

#define AOP_TAG                         'pOpA'

#define AOP_ASC_CPU_CONTROL             0x44
#define AOP_ASC_CPU_CONTROL_RUN         0x10
#define AOP_BOOTARGS_OFFSET             0x22c
#define AOP_BOOTARGS_SIZE               0x230
#define AOP_MMIO_SIZE                   0x1e0000
#define AOP_ASC_SIZE                    0x4000

#define AFK_ENDPOINT_START              0x20
#define AFK_ENDPOINT_COUNT              0x0f
#define AFK_OPCODE_SHIFT                48
#define AFK_OPC_INIT                    0x80ULL
#define AFK_OPC_RECV                    0x85ULL
#define AFK_OPC_START_ACK               0x86ULL
#define AFK_OPC_GET_BUF                 0x89ULL
#define AFK_OPC_INIT_TX                 0x8aULL
#define AFK_OPC_INIT_RX                 0x8bULL
#define AFK_OPC_INIT_UNK                0x8cULL
#define AFK_OPC_INIT_ACK                0xa0ULL
#define AFK_OPC_GET_BUF_ACK             0xa1ULL
#define AFK_OPC_SEND                    0xa2ULL
#define AFK_OPC_START                   0xa3ULL
#define AFK_OPC_SHUTDOWN                0xc0ULL
#define AFK_OPC_SHUTDOWN_ACK            0xc1ULL
#define AFK_RB_BLOCK_STEP               0x40
#define AFK_QE_MAGIC_INPUT              0x20504f49UL
#define AFK_QE_MAGIC_OUTPUT             0x20504f41UL

#define EPIC_TYPE_NOTIFY                0
#define EPIC_CATEGORY_REPORT            0x00
#define EPIC_CATEGORY_NOTIFY            0x10
#define EPIC_CATEGORY_REPLY             0x20
#define EPIC_SUBTYPE_STD_SERVICE        0xc0
#define EPIC_SUBTYPE_FAKEHID_REPORT     0xc4
#define EPIC_SUBTYPE_RETCODE            0x84
#define EPIC_SUBTYPE_RETCODE_PAYLOAD    0xa0
#define EPIC_SUBTYPE_STRING             0x8a
#define EPIC_MAX_CALLS                  8

#define AOP_MAX_DMA                     48
#define AOP_MAX_SERVICES                24
#define AOP_SERVICE_NAME                20
#define AOP_CALL_TIMEOUT_MS             1000
#define AOP_BOOT_TIMEOUT_MS             2000
#define AOP_POLL_INTERVAL_MS            20
#define AOP_SUMMARY_DELAY_MS            3000

#define AOP_TYPE_BUS                    0
#define AOP_SENSOR_ALS                  0
#define AOP_SENSOR_LAS                  1
#define AOP_SENSORS                     2
#define AOP_FUNCTION_AUDIO              2
#define AOP_FUNCTIONS                   3

#pragma pack(push, 1)
typedef struct _AFK_QE_HEADER
{
    ULONG Magic;
    ULONG Size;
    ULONG Channel;
    ULONG Type;
} AFK_QE_HEADER, *PAFK_QE_HEADER;

typedef struct _EPIC_HEADER
{
    UCHAR Version;
    USHORT Sequence;
    UCHAR Pad0;
    ULONG Unknown0;
    ULONG64 Timestamp;
    ULONG Length;
    UCHAR SubVersion;
    UCHAR Category;
    USHORT Subtype;
    USHORT Tag;
    USHORT Unknown1;
    ULONG64 Pad1;
    ULONG InlineLength;
} EPIC_HEADER, *PEPIC_HEADER;

typedef struct _EPIC_SERVICE_ANNOUNCE
{
    CHAR Name[AOP_SERVICE_NAME];
    ULONG Unknown0;
    ULONG ReturnCode;
    ULONG Unknown1;
    ULONG Channel;
    ULONG Unknown2;
    ULONG Unknown3;
} EPIC_SERVICE_ANNOUNCE, *PEPIC_SERVICE_ANNOUNCE;
#pragma pack(pop)

C_ASSERT(sizeof(AFK_QE_HEADER) == 16);
C_ASSERT(sizeof(EPIC_HEADER) == 40);
C_ASSERT(sizeof(EPIC_SERVICE_ANNOUNCE) == 44);

typedef struct _AOP_CONFIG
{
    PCSTR Compatible;
    ULONG64 Ec0p;
    ULONG64 Alig;
    ULONG64 Aopt;
} AOP_CONFIG, *PAOP_CONFIG;

typedef struct _AOP_DMA
{
    PVOID Virtual;
    ULONG64 Physical;
    ULONG64 Iova;
    ULONG Size;
} AOP_DMA, *PAOP_DMA;

typedef struct _AFK_RING
{
    BOOLEAN Valid;
    ULONG Offset;
    ULONG BlockSize;
    ULONG BufferSize;
} AFK_RING, *PAFK_RING;

typedef struct _EPIC_CALL
{
    BOOLEAN InUse;
    BOOLEAN Done;
    BOOLEAN Abandoned;
    ULONG ReturnCode;
    PUCHAR Reply;
    ULONG ReplyLength;
    ULONG Returned;
} EPIC_CALL, *PEPIC_CALL;

typedef struct _AFK_ENDPOINT
{
    UCHAR Index;
    BOOLEAN Present;
    BOOLEAN StartSent;
    BOOLEAN Started;
    BOOLEAN ShutdownAcked;
    PAOP_DMA Shared;
    AFK_RING Rx;
    AFK_RING Tx;
    USHORT Sequence;
    ULONG Messages;
    ULONG Errors;
    EPIC_CALL Calls[EPIC_MAX_CALLS];
} AFK_ENDPOINT, *PAFK_ENDPOINT;

typedef struct _AOP_SERVICE
{
    CHAR Name[AOP_SERVICE_NAME + 1];
    UCHAR Endpoint;
    ULONG Channel;
    BOOLEAN Handled;
} AOP_SERVICE, *PAOP_SERVICE;

typedef struct _AOP_SENSOR
{
    LONG Service;
    BOOLEAN Listening;
    BOOLEAN HaveValue;
    BOOLEAN Logged;
    ULONG Value;
    ULONG LoggedValue;
    ULONG Reports;
    ULONG Malformed;
    ULONG ReportOffset;
    PAPPLE_AOP_SAMPLE_CALLBACK Callback;
    PVOID CallbackContext;
    PDEVICE_OBJECT Child;
} AOP_SENSOR, *PAOP_SENSOR;

typedef struct _AOP_DEVICE
{
    ULONG Type;
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT Pdo;

    FDT_DRIVER_TREE Tree;
    BOOLEAN TreeOpen;
    const AOP_CONFIG *Config;

    PUCHAR Aop;
    ULONG AopSize;
    ULONG64 AopPhysical;
    PUCHAR Asc;
    ULONG AscSize;
    PUCHAR Mailbox;
    ULONG MailboxSize;

    ULONG DartNode;
    ULONG DartSid;
    BOOLEAN DartIdentity;
    BOOLEAN DartAdopted;
    APPLE_DART_ADOPTED Dart;
    AOP_DMA Dma[AOP_MAX_DMA];
    ULONG DmaCount;

    APPLE_RTKIT Rtkit;
    FAST_MUTEX Lock;
    PKTHREAD LockOwner;
    BOOLEAN Running;
    BOOLEAN FailureLogged;
    AFK_ENDPOINT Endpoints[AFK_ENDPOINT_COUNT];
    AOP_SERVICE Services[AOP_MAX_SERVICES];
    ULONG ServiceCount;
    volatile LONG ServicesPending;

    KSPIN_LOCK SensorLock;
    AOP_SENSOR Sensors[AOP_FUNCTIONS];
    FAST_MUTEX ChildLock;
    BOOLEAN ChildrenReported;

    PKTHREAD Thread;
    KEVENT StopEvent;
    KEVENT WorkEvent;
    volatile LONG Stop;
    ULONG64 StartedMs;
    BOOLEAN SummaryLogged;
} AOP_DEVICE, *PAOP_DEVICE;

typedef struct _AOP_CHILD
{
    ULONG Type;
    PDEVICE_OBJECT Self;
    PAOP_DEVICE Parent;
    ULONG Sensor;
    volatile LONG References;
} AOP_CHILD, *PAOP_CHILD;

ULONG64
AopNowMs(VOID);

VOID
AopLock(
    _Inout_ PAOP_DEVICE Device);

VOID
AopUnlock(
    _Inout_ PAOP_DEVICE Device);

PAOP_DMA
AopAllocateShared(
    _Inout_ PAOP_DEVICE Device,
    _In_ ULONG Size);

VOID
AopPatchBootArguments(
    _Inout_ PAOP_DEVICE Device);

VOID
AfkStartEndpoints(
    _Inout_ PAOP_DEVICE Device);

VOID
AfkStopEndpoints(
    _Inout_ PAOP_DEVICE Device);

VOID
NTAPI
AfkReceive(
    _In_ PVOID Context,
    _In_ UCHAR Endpoint,
    _In_ ULONG64 Message);

NTSTATUS
AopEpicCall(
    _Inout_ PAOP_DEVICE Device,
    _In_ ULONG Service,
    _In_ USHORT Subtype,
    _In_reads_bytes_(RequestLength) const VOID *Request,
    _In_ ULONG RequestLength,
    _Out_writes_bytes_opt_(ReplyLength) PVOID Reply,
    _In_ ULONG ReplyLength,
    _Out_opt_ PULONG Returned,
    _Out_ PULONG ReturnCode);

VOID
AopSensorsSetup(
    _Inout_ PAOP_DEVICE Device);

VOID
AopSensorReport(
    _Inout_ PAOP_DEVICE Device,
    _In_ ULONG Service,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length);
