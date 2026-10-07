/*
 * PROJECT:     LiberNT SDK Headers
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Event tracing logger structures shared by the kernel and its controllers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <wmistr.h>
#include <evntrace.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TRACE_HEADER_FLAG                   0x80000000
#define TRACE_HEADER_EVENT_TRACE            0x40000000
#define TRACE_HEADER_ENUM_MASK              0x00FF0000

#define TRACE_HEADER_TYPE_SYSTEM32          1
#define TRACE_HEADER_TYPE_SYSTEM64          2
#define TRACE_HEADER_TYPE_FULL_HEADER32     10
#define TRACE_HEADER_TYPE_FULL_HEADER64     20

#define SYSTEM_TRACE_VERSION                2

#define MAXLOGGERS                          80

#define EVENT_TRACE_CLOCK_RAW               0
#define EVENT_TRACE_CLOCK_PERFCOUNTER       1
#define EVENT_TRACE_CLOCK_SYSTEMTIME        2
#define EVENT_TRACE_CLOCK_CPUCYCLE          3
#define EVENT_TRACE_CLOCK_MAX               4

#define EVENT_TRACE_GROUP_HEADER             0x0000
#define WMI_LOG_TYPE_HEADER                 (EVENT_TRACE_GROUP_HEADER | EVENT_TRACE_TYPE_INFO)

#define ETW_BUFFER_TYPE_GENERIC             0
#define ETW_BUFFER_TYPE_HEADER              4

#define ETW_BUFFER_FLAG_NORMAL              0x0000
#define ETW_BUFFER_FLAG_FLUSH_MARKER        0x0001
#define ETW_BUFFER_FLAG_EVENTS_LOST         0x0002
#define ETW_BUFFER_FLAG_BUFFER_LOST         0x0004
#define ETW_BUFFER_FLAG_PROC_INDEX          0x0020

#define ETW_NT_TRACE_TYPE_MASK              0x0000FF00
#define ETW_NT_FLAGS_TRACE_HEADER           0x00000100
#define ETW_NT_FLAGS_TRACE_MESSAGE          0x00000200
#define ETW_NT_FLAGS_TRACE_EVENT            0x00000300
#define ETW_NT_FLAGS_TRACE_SYSTEM           0x00000400
#define ETW_NT_FLAGS_TRACE_SECURITY         0x00000500
#define ETW_NT_FLAGS_TRACE_MARK             0x00000600
#define ETW_NT_FLAGS_TRACE_EVENT_NOREG      0x00000700
#define ETW_NT_FLAGS_TRACE_INSTANCE         0x00000800

typedef enum _ETWTRACECONTROLCODE
{
    EtwStartLoggerCode = 1,
    EtwStopLoggerCode = 2,
    EtwQueryLoggerCode = 3,
    EtwUpdateLoggerCode = 4,
    EtwFlushLoggerCode = 5
} ETWTRACECONTROLCODE;

typedef struct _WMI_TRACE_PACKET
{
    USHORT Size;
    union
    {
        USHORT HookId;
        struct
        {
            UCHAR Type;
            UCHAR Group;
        } DUMMYSTRUCTNAME;
    } DUMMYUNIONNAME;
} WMI_TRACE_PACKET, *PWMI_TRACE_PACKET;

typedef struct _SYSTEM_TRACE_HEADER
{
    union
    {
        ULONG Marker;
        struct
        {
            USHORT Version;
            UCHAR HeaderType;
            UCHAR Flags;
        } DUMMYSTRUCTNAME;
    } DUMMYUNIONNAME;
    union
    {
        ULONG Header;
        WMI_TRACE_PACKET Packet;
    } DUMMYUNIONNAME2;
    ULONG ThreadId;
    ULONG ProcessId;
    LARGE_INTEGER SystemTime;
    ULONG KernelTime;
    ULONG UserTime;
} SYSTEM_TRACE_HEADER, *PSYSTEM_TRACE_HEADER;

typedef struct _ETW_REF_CLOCK
{
    LARGE_INTEGER StartTime;
    LARGE_INTEGER StartPerfClock;
} ETW_REF_CLOCK, *PETW_REF_CLOCK;

typedef enum _ETW_BUFFER_STATE
{
    EtwBufferStateFree,
    EtwBufferStateGeneralLogging,
    EtwBufferStateCSwitch,
    EtwBufferStateFlush,
    EtwBufferStateMaximum
} ETW_BUFFER_STATE, *PETW_BUFFER_STATE;

typedef struct _WMI_BUFFER_HEADER *PWMI_BUFFER_HEADER;

typedef struct _WMI_BUFFER_HEADER
{
    ULONG BufferSize;
    ULONG SavedOffset;
    volatile ULONG CurrentOffset;
    volatile LONG ReferenceCount;
    LARGE_INTEGER TimeStamp;
    LONGLONG SequenceNumber;
    union
    {
        struct
        {
            ULONGLONG ClockType : 3;
            ULONGLONG Frequency : 61;
        } DUMMYSTRUCTNAME;
        SINGLE_LIST_ENTRY SlistEntry;
        PWMI_BUFFER_HEADER NextBuffer;
    } DUMMYUNIONNAME;
    ETW_BUFFER_CONTEXT ClientContext;
    ETW_BUFFER_STATE State;
    ULONG Offset;
    USHORT BufferFlag;
    USHORT BufferType;
    union
    {
        ULONG Padding1[4];
        ETW_REF_CLOCK ReferenceTime;
        LIST_ENTRY GlobalEntry;
        struct
        {
            PVOID Pointer0;
            PVOID Pointer1;
        } DUMMYSTRUCTNAME2;
    } DUMMYUNIONNAME2;
} WMI_BUFFER_HEADER;

C_ASSERT(sizeof(WMI_BUFFER_HEADER) == 0x48);
C_ASSERT(FIELD_OFFSET(WMI_BUFFER_HEADER, TimeStamp) == 0x10);
C_ASSERT(FIELD_OFFSET(WMI_BUFFER_HEADER, ClientContext) == 0x28);
C_ASSERT(FIELD_OFFSET(WMI_BUFFER_HEADER, State) == 0x2c);
C_ASSERT(FIELD_OFFSET(WMI_BUFFER_HEADER, Offset) == 0x30);
C_ASSERT(FIELD_OFFSET(WMI_BUFFER_HEADER, BufferFlag) == 0x34);
C_ASSERT(FIELD_OFFSET(WMI_BUFFER_HEADER, BufferType) == 0x36);

typedef struct _WMI_LOGGER_INFORMATION
{
    WNODE_HEADER Wnode;
    ULONG BufferSize;
    ULONG MinimumBuffers;
    ULONG MaximumBuffers;
    ULONG MaximumFileSize;
    ULONG LogFileMode;
    ULONG FlushTimer;
    ULONG EnableFlags;
    union
    {
        LONG AgeLimit;
        LONG FlushThreshold;
    } DUMMYUNIONNAME;
    ULONG Wow;
    union
    {
        HANDLE LogFileHandle;
        ULONG64 LogFileHandle64;
    } DUMMYUNIONNAME2;
    union
    {
        ULONG NumberOfBuffers;
        ULONG InstanceCount;
    } DUMMYUNIONNAME3;
    union
    {
        ULONG FreeBuffers;
        ULONG InstanceId;
    } DUMMYUNIONNAME4;
    union
    {
        ULONG EventsLost;
        ULONG NumberOfProcessors;
    } DUMMYUNIONNAME5;
    ULONG BuffersWritten;
    union
    {
        ULONG LogBuffersLost;
        ULONG Flags;
    } DUMMYUNIONNAME6;
    ULONG RealTimeBuffersLost;
    union
    {
        HANDLE LoggerThreadId;
        ULONG64 LoggerThreadId64;
    } DUMMYUNIONNAME7;
    union
    {
        UNICODE_STRING LogFileName;
        UNICODE_STRING64 LogFileName64;
    } DUMMYUNIONNAME8;
    union
    {
        UNICODE_STRING LoggerName;
        UNICODE_STRING64 LoggerName64;
    } DUMMYUNIONNAME9;
    ULONG RealTimeConsumerCount;
    ULONG SpareUlong;
    union
    {
        PVOID LoggerExtension;
        ULONG64 LoggerExtension64;
    } DUMMYUNIONNAME10;
} WMI_LOGGER_INFORMATION, *PWMI_LOGGER_INFORMATION;

#ifdef __cplusplus
}
#endif
