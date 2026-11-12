/*
 * Copyright (C) 2017 Austin English
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef _PERFLIB_H_
#define _PERFLIB_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef void * (__WINE_ALLOC_SIZE(1) CALLBACK * PERF_MEM_ALLOC)(SIZE_T,void*);
typedef void (CALLBACK * PERF_MEM_FREE)(LPVOID,LPVOID);
typedef ULONG (WINAPI * PERFLIBREQUEST)(ULONG,PVOID,ULONG);

typedef struct _PERF_COUNTERSET_INFO {
    GUID CounterSetGuid;
    GUID ProviderGuid;
    ULONG NumCounters;
    ULONG InstanceType;
} PERF_COUNTERSET_INFO, * PPERF_COUNTERSET_INFO;

/* PERF_COUNTERSET_INFO InstanceType values. */
#define PERF_COUNTERSET_FLAG_MULTIPLE  0x00000002
#define PERF_COUNTERSET_FLAG_AGGREGATE 0x00000004
#define PERF_COUNTERSET_FLAG_HISTORY   0x00000008
#define PERF_COUNTERSET_FLAG_INSTANCE  0x00000010

#define PERF_COUNTERSET_SINGLE_INSTANCE          0
#define PERF_COUNTERSET_MULTI_INSTANCES          PERF_COUNTERSET_FLAG_MULTIPLE
#define PERF_COUNTERSET_SINGLE_AGGREGATE         PERF_COUNTERSET_FLAG_AGGREGATE
#define PERF_COUNTERSET_MULTI_AGGREGATE          (PERF_COUNTERSET_FLAG_AGGREGATE | PERF_COUNTERSET_FLAG_MULTIPLE)
#define PERF_COUNTERSET_SINGLE_AGGREGATE_HISTORY (PERF_COUNTERSET_FLAG_HISTORY | PERF_COUNTERSET_SINGLE_AGGREGATE)
#define PERF_COUNTERSET_INSTANCE_AGGREGATE       (PERF_COUNTERSET_MULTI_AGGREGATE | PERF_COUNTERSET_FLAG_INSTANCE)

typedef struct _PERF_COUNTERSET_INSTANCE {
    GUID CounterSetGuid;
    ULONG dwSize;
    ULONG InstanceId;
    ULONG InstanceNameOffset;
    ULONG InstanceNameSize;
} PERF_COUNTERSET_INSTANCE, * PPERF_COUNTERSET_INSTANCE;

typedef struct _PERF_COUNTER_INFO {
    ULONG CounterId;
    ULONG Type;
    ULONGLONG Attrib;
    ULONG Size;
    ULONG DetailLevel;
    LONG Scale;
    ULONG Offset;
} PERF_COUNTER_INFO, *PPERF_COUNTER_INFO;

/* PERF_COUNTER_INFO Attrib flags. */
#define PERF_ATTRIB_BY_REFERENCE       0x00000001
#define PERF_ATTRIB_NO_DISPLAYABLE     0x00000002
#define PERF_ATTRIB_NO_GROUP_SEPARATOR 0x00000004
#define PERF_ATTRIB_DISPLAY_AS_REAL    0x00000008
#define PERF_ATTRIB_DISPLAY_AS_HEX     0x00000010

typedef struct _PROVIDER_CONTEXT {
    DWORD ContextSize;
    DWORD Reserved;
    PERFLIBREQUEST ControlCallback;
    PERF_MEM_ALLOC MemAllocRoutine;
    PERF_MEM_FREE MemFreeRoutine;
    LPVOID pMemContext;
} PERF_PROVIDER_CONTEXT, * PPERF_PROVIDER_CONTEXT;

typedef struct _PERF_COUNTER_IDENTIFIER {
    GUID CounterSetGuid;
    ULONG Status;
    ULONG Size;
    ULONG CounterId;
    ULONG InstanceId;
    ULONG Index;
    ULONG Reserved;
} PERF_COUNTER_IDENTIFIER, *PPERF_COUNTER_IDENTIFIER;

#define PERF_WILDCARD_COUNTER  0xFFFFFFFF
#define PERF_WILDCARD_INSTANCE L"*"

typedef struct _PERF_DATA_HEADER {
    ULONG dwTotalSize;
    ULONG dwNumCounters;
    LONGLONG PerfTimeStamp;
    LONGLONG PerfTime100NSec;
    LONGLONG PerfFreq;
    SYSTEMTIME SystemTime;
} PERF_DATA_HEADER, *PPERF_DATA_HEADER;

#define PERF_PROVIDER_USER_MODE   0
#define PERF_PROVIDER_KERNEL_MODE 1
#define PERF_PROVIDER_DRIVER      2

#define PERF_AGGREGATE_UNDEFINED 0
#define PERF_AGGREGATE_TOTAL     1
#define PERF_AGGREGATE_AVG       2
#define PERF_AGGREGATE_MIN       3
#define PERF_AGGREGATE_MAX       4

typedef struct _PERF_COUNTER_IDENTITY {
    GUID CounterSetGuid;
    ULONG BufferSize;
    ULONG CounterId;
    ULONG InstanceId;
    ULONG MachineOffset;
    ULONG NameOffset;
    ULONG Reserved;
} PERF_COUNTER_IDENTITY, *PPERF_COUNTER_IDENTITY;

#define PERF_AGGREGATE_INSTANCE L"_Total"
#define PERF_MAX_INSTANCE_NAME  1024

#define PERF_ADD_COUNTER    1
#define PERF_REMOVE_COUNTER 2
#define PERF_ENUM_INSTANCES 3
#define PERF_COLLECT_START  5
#define PERF_COLLECT_END    6
#define PERF_FILTER         9

typedef struct _PERF_INSTANCE_HEADER {
    ULONG Size;
    ULONG InstanceId;
} PERF_INSTANCE_HEADER, *PPERF_INSTANCE_HEADER;

typedef enum _PerfRegInfoType {
    PERF_REG_COUNTERSET_STRUCT = 1,
    PERF_REG_COUNTER_STRUCT,
    PERF_REG_COUNTERSET_NAME_STRING,
    PERF_REG_COUNTERSET_HELP_STRING,
    PERF_REG_COUNTER_NAME_STRINGS,
    PERF_REG_COUNTER_HELP_STRINGS,
    PERF_REG_PROVIDER_NAME,
    PERF_REG_PROVIDER_GUID,
    PERF_REG_COUNTERSET_ENGLISH_NAME,
    PERF_REG_COUNTER_ENGLISH_NAMES
} PerfRegInfoType;

typedef struct _PERF_COUNTERSET_REG_INFO {
    GUID CounterSetGuid;
    ULONG CounterSetType;
    ULONG DetailLevel;
    ULONG NumCounters;
    ULONG InstanceType;
} PERF_COUNTERSET_REG_INFO, *PPERF_COUNTERSET_REG_INFO;

typedef struct _PERF_COUNTER_REG_INFO {
    ULONG CounterId;
    ULONG Type;
    ULONGLONG Attrib;
    ULONG DetailLevel;
    LONG DefaultScale;
    ULONG BaseCounterId;
    ULONG PerfTimeId;
    ULONG PerfFreqId;
    ULONG MultiId;
    ULONG AggregateFunc;
    ULONG Reserved;
} PERF_COUNTER_REG_INFO, *PPERF_COUNTER_REG_INFO;

typedef struct _STRING_BUFFER_HEADER {
    DWORD dwSize;
    DWORD dwCounters;
} PERF_STRING_BUFFER_HEADER, *PPERF_STRING_BUFFER_HEADER;

typedef struct _STRING_COUNTER_HEADER {
    DWORD dwCounterId;
    DWORD dwOffset;
} PERF_STRING_COUNTER_HEADER, *PPERF_STRING_COUNTER_HEADER;

typedef enum _PerfCounterDataType {
    PERF_ERROR_RETURN = 0,
    PERF_SINGLE_COUNTER = 1,
    PERF_MULTIPLE_COUNTERS = 2,
    PERF_MULTIPLE_INSTANCES = 4,
    PERF_COUNTERSET = 6
} PerfCounterDataType;

typedef struct _PERF_COUNTER_HEADER {
    ULONG dwStatus;
    PerfCounterDataType dwType;
    ULONG dwSize;
    ULONG Reserved;
} PERF_COUNTER_HEADER, *PPERF_COUNTER_HEADER;

typedef struct _PERF_MULTI_INSTANCES {
    ULONG dwTotalSize;
    ULONG dwInstances;
} PERF_MULTI_INSTANCES, *PPERF_MULTI_INSTANCES;

typedef struct _PERF_MULTI_COUNTERS {
    ULONG dwSize;
    ULONG dwCounters;
} PERF_MULTI_COUNTERS, *PPERF_MULTI_COUNTERS;

typedef struct _PERF_COUNTER_DATA {
    ULONG dwDataSize;
    ULONG dwSize;
} PERF_COUNTER_DATA, *PPERF_COUNTER_DATA;

#ifdef __REACTOS__
PERF_COUNTERSET_INSTANCE * WINAPI PerfCreateInstance(HANDLE, const GUID *, const WCHAR *, ULONG);
#else
PERF_COUNTERSET_INSTANCE WINAPI *PerfCreateInstance(HANDLE, const GUID *, const WCHAR *, ULONG);
#endif /* __REACTOS__ */
ULONG WINAPI PerfDeleteInstance(HANDLE, PERF_COUNTERSET_INSTANCE *);
PERF_COUNTERSET_INSTANCE * WINAPI PerfQueryInstance(HANDLE, const GUID *, const WCHAR *, ULONG);
ULONG WINAPI PerfIncrementULongCounterValue(HANDLE, PERF_COUNTERSET_INSTANCE *, ULONG, ULONG);
ULONG WINAPI PerfIncrementULongLongCounterValue(HANDLE, PERF_COUNTERSET_INSTANCE *, ULONG, ULONGLONG);
ULONG WINAPI PerfDecrementULongCounterValue(HANDLE, PERF_COUNTERSET_INSTANCE *, ULONG, ULONG);
ULONG WINAPI PerfDecrementULongLongCounterValue(HANDLE, PERF_COUNTERSET_INSTANCE *, ULONG, ULONGLONG);
ULONG WINAPI PerfSetCounterRefValue(HANDLE, PERF_COUNTERSET_INSTANCE *, ULONG, void *);
ULONG WINAPI PerfSetULongCounterValue(HANDLE, PERF_COUNTERSET_INSTANCE *, ULONG, ULONG);
ULONG WINAPI PerfSetULongLongCounterValue(HANDLE, PERF_COUNTERSET_INSTANCE *, ULONG, ULONGLONG);
ULONG WINAPI PerfSetCounterSetInfo(HANDLE, PERF_COUNTERSET_INFO *, ULONG);
ULONG WINAPI PerfStartProvider(GUID *, PERFLIBREQUEST, HANDLE *);
ULONG WINAPI PerfStartProviderEx(GUID *, PERF_PROVIDER_CONTEXT *, HANDLE *);
ULONG WINAPI PerfStopProvider(HANDLE);

ULONG WINAPI PerfAddCounters(HANDLE, PERF_COUNTER_IDENTIFIER *, DWORD);
ULONG WINAPI PerfCloseQueryHandle(HANDLE);
ULONG WINAPI PerfDeleteCounters(HANDLE, PERF_COUNTER_IDENTIFIER *, DWORD);
ULONG WINAPI PerfEnumerateCounterSet(LPCWSTR, LPGUID, DWORD, LPDWORD);
ULONG WINAPI PerfEnumerateCounterSetInstances(LPCWSTR, LPCGUID, PERF_INSTANCE_HEADER *, DWORD, LPDWORD);
ULONG WINAPI PerfOpenQueryHandle(const WCHAR *, HANDLE *);
ULONG WINAPI PerfQueryCounterData(HANDLE, PERF_DATA_HEADER *, DWORD, DWORD *);
ULONG WINAPI PerfQueryCounterInfo(HANDLE, PERF_COUNTER_IDENTIFIER *, DWORD, DWORD *);
ULONG WINAPI PerfQueryCounterSetRegistrationInfo(LPCWSTR, LPCGUID, PerfRegInfoType, DWORD, LPBYTE, DWORD, LPDWORD);

#ifdef __cplusplus
}       /* extern "C" */
#endif

#endif /* _PERFLIB_H_ */
