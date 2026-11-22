/*
 * Performance Data Helper
 *
 * Copyright 2007 Hans Leidekker
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

#ifndef _PDH_H_
#define _PDH_H_

#include <windows.h>
#include <winperf.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef LONG   PDH_STATUS;
typedef HANDLE PDH_HQUERY;
typedef HANDLE PDH_HCOUNTER;
typedef HANDLE PDH_HLOG;

typedef PDH_HCOUNTER HCOUNTER;
typedef PDH_HQUERY   HQUERY;
typedef PDH_HLOG     HLOG;

#define PDH_CVERSION_WIN40  0x0400
#define PDH_CVERSION_WIN50  0x0500
#define PDH_VERSION         0x0503

#define PDH_MAX_SCALE 7
#define PDH_MIN_SCALE (-7)

#define PDH_MAX_COUNTER_NAME    1024

#define PDH_FMT_LONG        0x00000100
#define PDH_FMT_DOUBLE      0x00000200
#define PDH_FMT_LARGE       0x00000400
#define PDH_FMT_NOSCALE     0x00001000
#define PDH_FMT_1000        0x00002000
#define PDH_FMT_NOCAP100    0x00008000

#define DATA_SOURCE_REGISTRY    0x00000001
#define DATA_SOURCE_LOGFILE     0x00000002
#define DATA_SOURCE_WBEM        0x00000004

#ifdef WINE_NO_UNICODE_MACROS
# define DECL_PDH_TYPE_AW(name)  /* nothing */
#else  /* WINE_NO_UNICODE_MACROS */
# ifdef UNICODE
# define DECL_PDH_TYPE_AW(name)  typedef name##_W name;
# else
# define DECL_PDH_TYPE_AW(name)  typedef name##_A name;
# endif
#endif  /* WINE_NO_UNICODE_MACROS */

typedef struct _PDH_FMT_COUNTERVALUE
{
    DWORD CStatus;
    union
    {
        LONG     longValue;
        double   doubleValue;
        LONGLONG largeValue;
        LPCSTR   AnsiStringValue;
        LPCWSTR  WideStringValue;
    } DUMMYUNIONNAME;
} PDH_FMT_COUNTERVALUE, *PPDH_FMT_COUNTERVALUE;

typedef struct _PDH_RAW_COUNTER
{
    DWORD    CStatus;
    FILETIME TimeStamp;
    LONGLONG FirstValue;
    LONGLONG SecondValue;
    DWORD    MultiCount;
} PDH_RAW_COUNTER, *PPDH_RAW_COUNTER;

typedef struct _PDH_COUNTER_PATH_ELEMENTS_A
{
    LPSTR szMachineName;
    LPSTR szObjectName;
    LPSTR szInstanceName;
    LPSTR szParentInstance;
    DWORD dwInstanceIndex;
    LPSTR szCounterName;
} PDH_COUNTER_PATH_ELEMENTS_A, *PPDH_COUNTER_PATH_ELEMENTS_A;

typedef struct _PDH_COUNTER_PATH_ELEMENTS_W
{
    LPWSTR szMachineName;
    LPWSTR szObjectName;
    LPWSTR szInstanceName;
    LPWSTR szParentInstance;
    DWORD  dwInstanceIndex;
    LPWSTR szCounterName;
} PDH_COUNTER_PATH_ELEMENTS_W, *PPDH_COUNTER_PATH_ELEMENTS_W;

DECL_PDH_TYPE_AW(PDH_COUNTER_PATH_ELEMENTS)
DECL_PDH_TYPE_AW(PPDH_COUNTER_PATH_ELEMENTS)

typedef struct _PDH_DATA_ITEM_PATH_ELEMENTS_A
{
    LPSTR szMachineName;
    GUID  ObjectGUID;
    DWORD dwItemId;
    LPSTR szInstanceName;
} PDH_DATA_ITEM_PATH_ELEMENTS_A, *PPDH_DATA_ITEM_PATH_ELEMENTS_A;

typedef struct _PDH_DATA_ITEM_PATH_ELEMENTS_W
{
    LPWSTR szMachineName;
    GUID   ObjectGUID;
    DWORD  dwItemId;
    LPWSTR szInstanceName;
} PDH_DATA_ITEM_PATH_ELEMENTS_W, *PPDH_DATA_ITEM_PATH_ELEMENTS_W;

DECL_PDH_TYPE_AW(PDH_DATA_ITEM_PATH_ELEMENTS)
DECL_PDH_TYPE_AW(PPDH_DATA_ITEM_PATH_ELEMENTS)

typedef struct _PDH_COUNTER_INFO_A
{
    DWORD     dwLength;
    DWORD     dwType;
    DWORD     CVersion;
    DWORD     CStatus;
    LONG      lScale;
    LONG      lDefaultScale;
    DWORD_PTR dwUserData;
    DWORD_PTR dwQueryUserData;
    LPSTR     szFullPath;
    union
    {
        PDH_DATA_ITEM_PATH_ELEMENTS_A DataItemPath;
        PDH_COUNTER_PATH_ELEMENTS_A   CounterPath;
        struct
        {
            LPSTR szMachineName;
            LPSTR szObjectName;
            LPSTR szInstanceName;
            LPSTR szParentInstance;
            DWORD dwInstanceIndex;
            LPSTR szCounterName;
        } DUMMYSTRUCTNAME;
    } DUMMYUNIONNAME;
    LPSTR szExplainText;
    DWORD DataBuffer[1];
} PDH_COUNTER_INFO_A, *PPDH_COUNTER_INFO_A;

typedef struct _PDH_COUNTER_INFO_W
{
    DWORD     dwLength;
    DWORD     dwType;
    DWORD     CVersion;
    DWORD     CStatus;
    LONG      lScale;
    LONG      lDefaultScale;
    DWORD_PTR dwUserData;
    DWORD_PTR dwQueryUserData;
    LPWSTR    szFullPath;
    union
    {
        PDH_DATA_ITEM_PATH_ELEMENTS_W DataItemPath;
        PDH_COUNTER_PATH_ELEMENTS_W   CounterPath;
        struct
        {
            LPWSTR szMachineName;
            LPWSTR szObjectName;
            LPWSTR szInstanceName;
            LPWSTR szParentInstance;
            DWORD  dwInstanceIndex;
            LPWSTR szCounterName;
        } DUMMYSTRUCTNAME;
    } DUMMYUNIONNAME;
    LPWSTR szExplainText;
    DWORD DataBuffer[1];
} PDH_COUNTER_INFO_W, *PPDH_COUNTER_INFO_W;

typedef struct _PDH_FMT_COUNTERVALUE_ITEM_A
{
    LPSTR szName;
    PDH_FMT_COUNTERVALUE FmtValue;
} PDH_FMT_COUNTERVALUE_ITEM_A,*PPDH_FMT_COUNTERVALUE_ITEM_A;

typedef struct _PDH_FMT_COUNTERVALUE_ITEM_W
{
    LPWSTR szName;
    PDH_FMT_COUNTERVALUE FmtValue;
} PDH_FMT_COUNTERVALUE_ITEM_W,*PPDH_FMT_COUNTERVALUE_ITEM_W;

DECL_PDH_TYPE_AW(PDH_FMT_COUNTERVALUE_ITEM)
DECL_PDH_TYPE_AW(PPDH_FMT_COUNTERVALUE_ITEM)

typedef struct _PDH_RAW_COUNTER_ITEM_A
{
    LPSTR szName;
    PDH_RAW_COUNTER RawValue;
} PDH_RAW_COUNTER_ITEM_A,*PPDH_RAW_COUNTER_ITEM_A;

typedef struct _PDH_RAW_COUNTER_ITEM_W
{
    LPWSTR szName;
    PDH_RAW_COUNTER RawValue;
} PDH_RAW_COUNTER_ITEM_W,*PPDH_RAW_COUNTER_ITEM_W;

DECL_PDH_TYPE_AW(PDH_COUNTER_INFO)
DECL_PDH_TYPE_AW(PPDH_COUNTER_INFO)

#undef DECL_PDH_TYPE_AW

PDH_STATUS WINAPI PdhAddCounterA(PDH_HQUERY, LPCSTR, DWORD_PTR, PDH_HCOUNTER *);
PDH_STATUS WINAPI PdhAddCounterW(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER *);
#define    PdhAddCounter WINELIB_NAME_AW(PdhAddCounter)
PDH_STATUS WINAPI PdhAddEnglishCounterA(PDH_HQUERY, LPCSTR, DWORD_PTR, PDH_HCOUNTER *);
PDH_STATUS WINAPI PdhAddEnglishCounterW(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER *);
#define    PdhAddEnglishCounter WINELIB_NAME_AW(PdhAddEnglishCounter)
PDH_STATUS WINAPI PdhBindInputDataSourceA(PDH_HLOG *, const char *);
PDH_STATUS WINAPI PdhBindInputDataSourceW(PDH_HLOG *, const WCHAR *);
#define    PdhBindInputDataSource WINELIB_NAME_AW(PdhBindInputDataSource)
PDH_STATUS WINAPI PdhCloseQuery(PDH_HQUERY);
PDH_STATUS WINAPI PdhCollectQueryData(PDH_HQUERY);
PDH_STATUS WINAPI PdhCollectQueryDataEx(PDH_HQUERY, DWORD, HANDLE);
PDH_STATUS WINAPI PdhCollectQueryDataWithTime(PDH_HQUERY,LONGLONG *);
PDH_STATUS WINAPI PdhEnumObjectsA(LPCSTR,LPCSTR,PZZSTR,LPDWORD,DWORD,BOOL);
PDH_STATUS WINAPI PdhEnumObjectsW(LPCWSTR,LPCWSTR,PZZWSTR,LPDWORD,DWORD,BOOL);
#define    PdhEnumObjects WINELIB_NAME_AW(PdhEnumObjects)
PDH_STATUS WINAPI PdhEnumObjectItemsA(LPCSTR, LPCSTR, LPCSTR, LPSTR, LPDWORD, LPSTR, LPDWORD, DWORD, DWORD);
PDH_STATUS WINAPI PdhEnumObjectItemsW(LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, LPDWORD, LPWSTR, LPDWORD, DWORD, DWORD);
#define    PdhEnumObjectItems WINELIB_NAME_AW(PdhEnumObjectItems)
PDH_STATUS WINAPI PdhGetCounterInfoA(PDH_HCOUNTER, BOOLEAN, LPDWORD, PPDH_COUNTER_INFO_A);
PDH_STATUS WINAPI PdhGetCounterInfoW(PDH_HCOUNTER, BOOLEAN, LPDWORD, PPDH_COUNTER_INFO_W);
#define    PdhGetCounterInfo WINELIB_NAME_AW(PdhGetCounterInfo)
PDH_STATUS WINAPI PdhGetCounterTimeBase(PDH_HCOUNTER, LONGLONG *);
PDH_STATUS WINAPI PdhGetDllVersion(LPDWORD);
PDH_STATUS WINAPI PdhGetFormattedCounterArrayA(PDH_HCOUNTER, DWORD, LPDWORD, LPDWORD, PPDH_FMT_COUNTERVALUE_ITEM_A);
PDH_STATUS WINAPI PdhGetFormattedCounterArrayW(PDH_HCOUNTER, DWORD, LPDWORD, LPDWORD, PPDH_FMT_COUNTERVALUE_ITEM_W);
#define    PdhGetFormattedCounterArray WINELIB_NAME_AW(PdhGetFormattedCounterArray)
PDH_STATUS WINAPI PdhGetFormattedCounterValue(PDH_HCOUNTER, DWORD, LPDWORD, PPDH_FMT_COUNTERVALUE);
PDH_STATUS WINAPI PdhGetRawCounterArrayA(PDH_HCOUNTER, LPDWORD, LPDWORD, PPDH_RAW_COUNTER_ITEM_A);
PDH_STATUS WINAPI PdhGetRawCounterArrayW(PDH_HCOUNTER, LPDWORD, LPDWORD, PPDH_RAW_COUNTER_ITEM_W);
#define    PdhGetRawCounterArray WINELIB_NAME_AW(PdhGetRawCounterArray)
PDH_STATUS WINAPI PdhGetRawCounterValue(PDH_HCOUNTER, LPDWORD, PPDH_RAW_COUNTER);
PDH_STATUS WINAPI PdhLookupPerfIndexByNameA(LPCSTR, LPCSTR, LPDWORD);
PDH_STATUS WINAPI PdhLookupPerfIndexByNameW(LPCWSTR, LPCWSTR, LPDWORD);
#define    PdhLookupPerfIndexByName WINELIB_NAME_AW(PdhLookupPerfIndexByName)
PDH_STATUS WINAPI PdhLookupPerfNameByIndexA(LPCSTR, DWORD, LPSTR, LPDWORD);
PDH_STATUS WINAPI PdhLookupPerfNameByIndexW(LPCWSTR, DWORD, LPWSTR, LPDWORD);
#define    PdhLookupPerfNameByIndex WINELIB_NAME_AW(PdhLookupPerfNameByIndex)
PDH_STATUS WINAPI PdhMakeCounterPathA(PDH_COUNTER_PATH_ELEMENTS_A *, LPSTR, LPDWORD, DWORD);
PDH_STATUS WINAPI PdhMakeCounterPathW(PDH_COUNTER_PATH_ELEMENTS_W *, LPWSTR, LPDWORD, DWORD);
#define    PdhMakeCounterPath WINELIB_NAME_AW(PdhMakeCounterPath)
PDH_STATUS WINAPI PdhOpenQueryA(LPCSTR, DWORD_PTR, PDH_HQUERY *);
PDH_STATUS WINAPI PdhOpenQueryW(LPCWSTR, DWORD_PTR, PDH_HQUERY *);
#define    PdhOpenQuery WINELIB_NAME_AW(PdhOpenQuery)
PDH_STATUS WINAPI PdhRemoveCounter(PDH_HCOUNTER);
PDH_STATUS WINAPI PdhSetCounterScaleFactor(PDH_HCOUNTER, LONG);
PDH_STATUS WINAPI PdhSetDefaultRealTimeDataSource(DWORD);
PDH_STATUS WINAPI PdhValidatePathA(LPCSTR);
PDH_STATUS WINAPI PdhValidatePathW(LPCWSTR);
#define    PdhValidatePath WINELIB_NAME_AW(PdhValidatePath)
PDH_STATUS WINAPI PdhValidatePathExA(PDH_HLOG, LPCSTR);
PDH_STATUS WINAPI PdhValidatePathExW(PDH_HLOG, LPCWSTR);
#define    PdhValidatePathEx WINELIB_NAME_AW(PdhValidatePathEx)

#ifdef __cplusplus
}
#endif

#ifndef MAX_COUNTER_PATH
#define MAX_COUNTER_PATH 256
#endif
#ifndef PDH_LOG_TYPE_BINARY
#define PDH_LOG_TYPE_BINARY 8
#endif
#ifndef PDH_LOG_TYPE_CSV
#define PDH_LOG_TYPE_CSV 1
#endif
#ifndef PDH_LOG_TYPE_PERFMON
#define PDH_LOG_TYPE_PERFMON 6
#endif
#ifndef PDH_LOG_TYPE_RETIRED_BIN
#define PDH_LOG_TYPE_RETIRED_BIN 3
#endif
#ifndef PDH_LOG_TYPE_SQL
#define PDH_LOG_TYPE_SQL 7
#endif
#ifndef PDH_LOG_TYPE_TRACE_GENERIC
#define PDH_LOG_TYPE_TRACE_GENERIC 5
#endif
#ifndef PDH_LOG_TYPE_TRACE_KERNEL
#define PDH_LOG_TYPE_TRACE_KERNEL 4
#endif
#ifndef PDH_LOG_TYPE_TSV
#define PDH_LOG_TYPE_TSV 2
#endif
#ifndef PDH_LOG_TYPE_UNDEFINED
#define PDH_LOG_TYPE_UNDEFINED 0
#endif
#ifndef PDH_MAX_COUNTER_PATH
#define PDH_MAX_COUNTER_PATH 2048
#endif
#ifndef PDH_MAX_DATASOURCE_PATH
#define PDH_MAX_DATASOURCE_PATH 1024
#endif
#ifndef PDH_MAX_INSTANCE_NAME
#define PDH_MAX_INSTANCE_NAME 1024
#endif
#ifndef PDH_NOEXPANDCOUNTERS
#define PDH_NOEXPANDCOUNTERS 1
#endif
#ifndef PDH_NOEXPANDINSTANCES
#define PDH_NOEXPANDINSTANCES 2
#endif
#ifndef PDH_REFRESHCOUNTERS
#define PDH_REFRESHCOUNTERS 4
#endif

#ifdef __cplusplus
extern "C" {
#endif
PDH_STATUS WINAPI PdhConnectMachineA(LPCSTR szMachineName);
PDH_STATUS WINAPI PdhConnectMachineW(LPCWSTR szMachineName);
#ifdef __cplusplus
}
#endif

typedef struct _PDH_RAW_LOG_RECORD {
  DWORD dwStructureSize;
  DWORD dwRecordType;
  DWORD dwItems;
  UCHAR RawBytes[1];
} PDH_RAW_LOG_RECORD, *PPDH_RAW_LOG_RECORD;

typedef struct _PDH_STATISTICS {
  DWORD dwFormat;
  DWORD count;
  PDH_FMT_COUNTERVALUE min;
  PDH_FMT_COUNTERVALUE max;
  PDH_FMT_COUNTERVALUE mean;
} PDH_STATISTICS, *PPDH_STATISTICS;

typedef struct _PDH_TIME_INFO {
  LONGLONG StartTime;
  LONGLONG EndTime;
  DWORD SampleCount;
} PDH_TIME_INFO, *PPDH_TIME_INFO;

#define H_REALTIME_DATASOURCE 0
#ifdef _WIN64
#define H_WBEM_DATASOURCE (-1)
#else
#define H_WBEM_DATASOURCE 0xFFFFFFFF
#endif
#define MAX_TIME_VALUE ((LONGLONG)0x7FFFFFFFFFFFFFFFULL)
#define MIN_TIME_VALUE ((LONGLONG)0)
#ifdef UNICODE
#define PDH_BROWSE_DLG_CONFIG PDH_BROWSE_DLG_CONFIG_W
#else
#if (!(defined(UNICODE)))
#define PDH_BROWSE_DLG_CONFIG PDH_BROWSE_DLG_CONFIG_A
#endif
#endif
#define PDH_FLAGS_CLOSE_QUERY ((DWORD)1)
#define PDH_FLAGS_FILE_BROWSER_ONLY ((DWORD)1)
#define PDH_FMT_ANSI ((DWORD)0x20)
#define PDH_FMT_NODATA ((DWORD)0x4000)
#define PDH_FMT_RAW ((DWORD)0x10)
#define PDH_FMT_UNICODE ((DWORD)0x40)
#define PDH_FUNCTION 0
#define PDH_LOG_ACCESS_MASK ((DWORD)0xF0000)
#define PDH_LOG_CREATE_ALWAYS ((DWORD)2)
#define PDH_LOG_CREATE_MASK ((DWORD)0xF)
#define PDH_LOG_CREATE_NEW ((DWORD)1)
#define PDH_LOG_OPEN_ALWAYS ((DWORD)3)
#define PDH_LOG_OPEN_EXISTING ((DWORD)4)
#define PDH_LOG_OPT_APPEND ((DWORD)0x8000000)
#define PDH_LOG_OPT_CIRCULAR ((DWORD)0x2000000)
#define PDH_LOG_OPT_MASK ((DWORD)0xF000000)
#define PDH_LOG_OPT_MAX_IS_BYTES ((DWORD)0x4000000)
#define PDH_LOG_OPT_USER_STRING ((DWORD)0x1000000)
#define PDH_LOG_READ_ACCESS ((DWORD)0x10000)
#ifdef UNICODE
#define PDH_LOG_SERVICE_QUERY_INFO PDH_LOG_SERVICE_QUERY_INFO_W
#else
#if (!(defined(UNICODE)))
#define PDH_LOG_SERVICE_QUERY_INFO PDH_LOG_SERVICE_QUERY_INFO_A
#endif
#endif
#define PDH_LOG_UPDATE_ACCESS ((DWORD)0x40000)
#define PDH_LOG_WRITE_ACCESS ((DWORD)0x20000)
#define PDH_OBJECT_HAS_INSTANCES ((DWORD)1)
#define PDH_PATH_WBEM_INPUT ((DWORD)2)
#define PDH_PATH_WBEM_RESULT ((DWORD)1)
#ifdef UNICODE
#define PDH_RAW_COUNTER_ITEM PDH_RAW_COUNTER_ITEM_W
#else
#if (!(defined(UNICODE)))
#define PDH_RAW_COUNTER_ITEM PDH_RAW_COUNTER_ITEM_A
#endif
#endif
#ifdef UNICODE
#define PdhBrowseCounters PdhBrowseCountersW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PdhBrowseCounters PdhBrowseCountersW
#else
#define PdhBrowseCounters PdhBrowseCountersA
#endif
#endif
#endif
#ifdef UNICODE
#define PdhBrowseCountersH PdhBrowseCountersHW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PdhBrowseCountersH PdhBrowseCountersHW
#else
#define PdhBrowseCountersH PdhBrowseCountersHA
#endif
#endif
#endif
#ifdef UNICODE
#define PdhConnectMachine PdhConnectMachineW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PdhConnectMachine PdhConnectMachineW
#else
#define PdhConnectMachine PdhConnectMachineA
#endif
#endif
#endif
#ifdef UNICODE
#define PdhCreateSQLTables PdhCreateSQLTablesW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PdhCreateSQLTables PdhCreateSQLTablesW
#else
#define PdhCreateSQLTables PdhCreateSQLTablesA
#endif
#endif
#endif
#ifdef UNICODE
#define PdhEnumLogSetNames PdhEnumLogSetNamesW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PdhEnumLogSetNames PdhEnumLogSetNamesW
#else
#define PdhEnumLogSetNames PdhEnumLogSetNamesA
#endif
#endif
#endif
#ifdef UNICODE
#define PdhEnumMachines PdhEnumMachinesW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PdhEnumMachines PdhEnumMachinesW
#else
#define PdhEnumMachines PdhEnumMachinesA
#endif
#endif
#endif
#ifdef UNICODE
#define PdhEnumMachinesH PdhEnumMachinesHW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PdhEnumMachinesH PdhEnumMachinesHW
#else
#define PdhEnumMachinesH PdhEnumMachinesHA
#endif
#endif
#endif
#ifdef UNICODE
#define PdhEnumObjectItemsH PdhEnumObjectItemsHW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PdhEnumObjectItemsH PdhEnumObjectItemsHW
#else
#define PdhEnumObjectItemsH PdhEnumObjectItemsHA
#endif
#endif
#endif
#ifdef UNICODE
#define PdhEnumObjectsH PdhEnumObjectsHW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PdhEnumObjectsH PdhEnumObjectsHW
#else
#define PdhEnumObjectsH PdhEnumObjectsHA
#endif
#endif
#endif
#ifdef UNICODE
#define PdhExpandCounterPath PdhExpandCounterPathW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PdhExpandCounterPath PdhExpandCounterPathW
#else
#define PdhExpandCounterPath PdhExpandCounterPathA
#endif
#endif
#endif
#ifdef UNICODE
#define PdhExpandWildCardPath PdhExpandWildCardPathW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PdhExpandWildCardPath PdhExpandWildCardPathW
#else
#define PdhExpandWildCardPath PdhExpandWildCardPathA
#endif
#endif
#endif
#ifdef UNICODE
#define PdhExpandWildCardPathH PdhExpandWildCardPathHW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PdhExpandWildCardPathH PdhExpandWildCardPathHW
#else
#define PdhExpandWildCardPathH PdhExpandWildCardPathHA
#endif
#endif
#endif
#ifdef UNICODE
#define PdhGetDataSourceTimeRange PdhGetDataSourceTimeRangeW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PdhGetDataSourceTimeRange PdhGetDataSourceTimeRangeW
#else
#define PdhGetDataSourceTimeRange PdhGetDataSourceTimeRangeA
#endif
#endif
#endif
#ifdef UNICODE
#define PdhGetDefaultPerfCounter PdhGetDefaultPerfCounterW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PdhGetDefaultPerfCounter PdhGetDefaultPerfCounterW
#else
#define PdhGetDefaultPerfCounter PdhGetDefaultPerfCounterA
#endif
#endif
#endif
#ifdef UNICODE
#define PdhGetDefaultPerfCounterH PdhGetDefaultPerfCounterHW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PdhGetDefaultPerfCounterH PdhGetDefaultPerfCounterHW
#else
#define PdhGetDefaultPerfCounterH PdhGetDefaultPerfCounterHA
#endif
#endif
#endif
#ifdef UNICODE
#define PdhGetDefaultPerfObject PdhGetDefaultPerfObjectW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PdhGetDefaultPerfObject PdhGetDefaultPerfObjectW
#else
#define PdhGetDefaultPerfObject PdhGetDefaultPerfObjectA
#endif
#endif
#endif
#ifdef UNICODE
#define PdhGetDefaultPerfObjectH PdhGetDefaultPerfObjectHW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PdhGetDefaultPerfObjectH PdhGetDefaultPerfObjectHW
#else
#define PdhGetDefaultPerfObjectH PdhGetDefaultPerfObjectHA
#endif
#endif
#endif
#ifdef UNICODE
#define PdhOpenLog PdhOpenLogW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PdhOpenLog PdhOpenLogW
#else
#define PdhOpenLog PdhOpenLogA
#endif
#endif
#endif
#ifdef UNICODE
#define PdhParseCounterPath PdhParseCounterPathW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PdhParseCounterPath PdhParseCounterPathW
#else
#define PdhParseCounterPath PdhParseCounterPathA
#endif
#endif
#endif
#ifdef UNICODE
#define PdhParseInstanceName PdhParseInstanceNameW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PdhParseInstanceName PdhParseInstanceNameW
#else
#define PdhParseInstanceName PdhParseInstanceNameA
#endif
#endif
#endif
#ifdef UNICODE
#define PdhSelectDataSource PdhSelectDataSourceW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PdhSelectDataSource PdhSelectDataSourceW
#else
#define PdhSelectDataSource PdhSelectDataSourceA
#endif
#endif
#endif
#ifdef UNICODE
#define PdhUpdateLog PdhUpdateLogW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PdhUpdateLog PdhUpdateLogW
#else
#define PdhUpdateLog PdhUpdateLogA
#endif
#endif
#endif
#ifdef UNICODE
#define PdhVerifySQLDB PdhVerifySQLDBW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PdhVerifySQLDB PdhVerifySQLDBW
#else
#define PdhVerifySQLDB PdhVerifySQLDBA
#endif
#endif
#endif
#define PERF_DETAIL_COSTLY ((DWORD)0x10000)
#define PERF_DETAIL_STANDARD ((DWORD)0xFFFF)
#ifdef UNICODE
#define PPDH_BROWSE_DLG_CONFIG PPDH_BROWSE_DLG_CONFIG_W
#else
#if (!(defined(UNICODE)))
#define PPDH_BROWSE_DLG_CONFIG PPDH_BROWSE_DLG_CONFIG_A
#endif
#endif
#ifdef UNICODE
#define PPDH_LOG_SERVICE_QUERY_INFO PPDH_LOG_SERVICE_QUERY_INFO_W
#else
#if (!(defined(UNICODE)))
#define PPDH_LOG_SERVICE_QUERY_INFO PPDH_LOG_SERVICE_QUERY_INFO_A
#endif
#endif
#ifdef UNICODE
#define PPDH_RAW_COUNTER_ITEM PPDH_RAW_COUNTER_ITEM_W
#else
#if (!(defined(UNICODE)))
#define PPDH_RAW_COUNTER_ITEM PPDH_RAW_COUNTER_ITEM_A
#endif
#endif

struct _PDH_LOG_SERVICE_QUERY_INFO_A
{
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwLogQuota;
    LPSTR szLogFileCaption;
    LPSTR szDefaultDir;
    LPSTR szBaseFileName;
    DWORD dwFileType;
    DWORD dwReserved;
    union
    {
        struct
        {
            DWORD PdlAutoNameInterval;
            DWORD PdlAutoNameUnits;
            LPSTR PdlCommandFilename;
            LPSTR PdlCounterList;
            DWORD PdlAutoNameFormat;
            DWORD PdlSampleInterval;
            FILETIME PdlLogStartTime;
            FILETIME PdlLogEndTime;
        };
        struct
        {
            DWORD TlNumberOfBuffers;
            DWORD TlMinimumBuffers;
            DWORD TlMaximumBuffers;
            DWORD TlFreeBuffers;
            DWORD TlBufferSize;
            DWORD TlEventsLost;
            DWORD TlLoggerThreadId;
            DWORD TlBuffersWritten;
            DWORD TlLogHandle;
            LPSTR TlLogFileName;
        };
    };
};

struct _PDH_LOG_SERVICE_QUERY_INFO_W
{
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwLogQuota;
    LPWSTR szLogFileCaption;
    LPWSTR szDefaultDir;
    LPWSTR szBaseFileName;
    DWORD dwFileType;
    DWORD dwReserved;
    union
    {
        struct
        {
            DWORD PdlAutoNameInterval;
            DWORD PdlAutoNameUnits;
            LPWSTR PdlCommandFilename;
            LPWSTR PdlCounterList;
            DWORD PdlAutoNameFormat;
            DWORD PdlSampleInterval;
            FILETIME PdlLogStartTime;
            FILETIME PdlLogEndTime;
        };
        struct
        {
            DWORD TlNumberOfBuffers;
            DWORD TlMinimumBuffers;
            DWORD TlMaximumBuffers;
            DWORD TlFreeBuffers;
            DWORD TlBufferSize;
            DWORD TlEventsLost;
            DWORD TlLoggerThreadId;
            DWORD TlBuffersWritten;
            DWORD TlLogHandle;
            LPWSTR TlLogFileName;
        };
    };
};

typedef PDH_STATUS (WINAPI *CounterPathCallBack)(DWORD_PTR);

struct _BrowseDlgConfig_A
{
    DWORD bIncludeInstanceIndex : 1;
    DWORD bSingleCounterPerAdd : 1;
    DWORD bSingleCounterPerDialog : 1;
    DWORD bLocalCountersOnly : 1;
    DWORD bWildCardInstances : 1;
    DWORD bHideDetailBox : 1;
    DWORD bInitializePath : 1;
    DWORD bDisableMachineSelection : 1;
    DWORD bIncludeCostlyObjects : 1;
    DWORD bShowObjectBrowser : 1;
    DWORD bReserved : 22;
    HWND hWndOwner;
    LPSTR szDataSource;
    LPSTR szReturnPathBuffer;
    DWORD cchReturnPathLength;
    CounterPathCallBack pCallBack;
    DWORD_PTR dwCallBackArg;
    PDH_STATUS CallBackStatus;
    DWORD dwDefaultDetailLevel;
    LPSTR szDialogBoxCaption;
};

struct _BrowseDlgConfig_HA
{
    DWORD bIncludeInstanceIndex : 1;
    DWORD bSingleCounterPerAdd : 1;
    DWORD bSingleCounterPerDialog : 1;
    DWORD bLocalCountersOnly : 1;
    DWORD bWildCardInstances : 1;
    DWORD bHideDetailBox : 1;
    DWORD bInitializePath : 1;
    DWORD bDisableMachineSelection : 1;
    DWORD bIncludeCostlyObjects : 1;
    DWORD bShowObjectBrowser : 1;
    DWORD bReserved : 22;
    HWND hWndOwner;
    PDH_HLOG hDataSource;
    LPSTR szReturnPathBuffer;
    DWORD cchReturnPathLength;
    CounterPathCallBack pCallBack;
    DWORD_PTR dwCallBackArg;
    PDH_STATUS CallBackStatus;
    DWORD dwDefaultDetailLevel;
    LPSTR szDialogBoxCaption;
};

struct _BrowseDlgConfig_HW
{
    DWORD bIncludeInstanceIndex : 1;
    DWORD bSingleCounterPerAdd : 1;
    DWORD bSingleCounterPerDialog : 1;
    DWORD bLocalCountersOnly : 1;
    DWORD bWildCardInstances : 1;
    DWORD bHideDetailBox : 1;
    DWORD bInitializePath : 1;
    DWORD bDisableMachineSelection : 1;
    DWORD bIncludeCostlyObjects : 1;
    DWORD bShowObjectBrowser : 1;
    DWORD bReserved : 22;
    HWND hWndOwner;
    PDH_HLOG hDataSource;
    LPWSTR szReturnPathBuffer;
    DWORD cchReturnPathLength;
    CounterPathCallBack pCallBack;
    DWORD_PTR dwCallBackArg;
    PDH_STATUS CallBackStatus;
    DWORD dwDefaultDetailLevel;
    LPWSTR szDialogBoxCaption;
};

struct _BrowseDlgConfig_W
{
    DWORD bIncludeInstanceIndex : 1;
    DWORD bSingleCounterPerAdd : 1;
    DWORD bSingleCounterPerDialog : 1;
    DWORD bLocalCountersOnly : 1;
    DWORD bWildCardInstances : 1;
    DWORD bHideDetailBox : 1;
    DWORD bInitializePath : 1;
    DWORD bDisableMachineSelection : 1;
    DWORD bIncludeCostlyObjects : 1;
    DWORD bShowObjectBrowser : 1;
    DWORD bReserved : 22;
    HWND hWndOwner;
    LPWSTR szDataSource;
    LPWSTR szReturnPathBuffer;
    DWORD cchReturnPathLength;
    CounterPathCallBack pCallBack;
    DWORD_PTR dwCallBackArg;
    PDH_STATUS CallBackStatus;
    DWORD dwDefaultDetailLevel;
    LPWSTR szDialogBoxCaption;
};

typedef struct _BrowseDlgConfig_A PDH_BROWSE_DLG_CONFIG_A;

typedef struct _BrowseDlgConfig_HA PDH_BROWSE_DLG_CONFIG_HA;

typedef struct _BrowseDlgConfig_HW PDH_BROWSE_DLG_CONFIG_HW;

typedef struct _BrowseDlgConfig_W PDH_BROWSE_DLG_CONFIG_W;

typedef struct _PDH_LOG_SERVICE_QUERY_INFO_A PDH_LOG_SERVICE_QUERY_INFO_A;

typedef struct _PDH_LOG_SERVICE_QUERY_INFO_W PDH_LOG_SERVICE_QUERY_INFO_W;

typedef struct _BrowseDlgConfig_A *PPDH_BROWSE_DLG_CONFIG_A;

typedef struct _BrowseDlgConfig_HA *PPDH_BROWSE_DLG_CONFIG_HA;

typedef struct _BrowseDlgConfig_HW *PPDH_BROWSE_DLG_CONFIG_HW;

typedef struct _BrowseDlgConfig_W *PPDH_BROWSE_DLG_CONFIG_W;

typedef struct _PDH_LOG_SERVICE_QUERY_INFO_A *PPDH_LOG_SERVICE_QUERY_INFO_A;

typedef struct _PDH_LOG_SERVICE_QUERY_INFO_W *PPDH_LOG_SERVICE_QUERY_INFO_W;

#ifdef __cplusplus
extern "C" {
#endif

PDH_STATUS WINAPI PdhCalculateCounterFromRawValue(PDH_HCOUNTER hCounter, DWORD dwFormat, PPDH_RAW_COUNTER rawValue1, PPDH_RAW_COUNTER rawValue2, PPDH_FMT_COUNTERVALUE fmtValue);
PDH_STATUS WINAPI PdhEnumMachinesA(LPCSTR szDataSource, PZZSTR mszMachineList, LPDWORD pcchBufferSize);
PDH_STATUS WINAPI PdhEnumMachinesW(LPCWSTR szDataSource, PZZWSTR mszMachineList, LPDWORD pcchBufferSize);
PDH_STATUS WINAPI PdhExpandCounterPathA(LPCSTR szWildCardPath, PZZSTR mszExpandedPathList, LPDWORD pcchPathListLength);
PDH_STATUS WINAPI PdhExpandCounterPathW(LPCWSTR szWildCardPath, PZZWSTR mszExpandedPathList, LPDWORD pcchPathListLength);
PDH_STATUS WINAPI PdhExpandWildCardPathA(LPCSTR szDataSource, LPCSTR szWildCardPath, PZZSTR mszExpandedPathList, LPDWORD pcchPathListLength, DWORD dwFlags);
PDH_STATUS WINAPI PdhExpandWildCardPathW(LPCWSTR szDataSource, LPCWSTR szWildCardPath, PZZWSTR mszExpandedPathList, LPDWORD pcchPathListLength, DWORD dwFlags);

/* TODO: LiberNT does not implement the functions below yet */
PDH_STATUS WINAPI PdhBrowseCountersA(PPDH_BROWSE_DLG_CONFIG_A pBrowseDlgData);
PDH_STATUS WINAPI PdhBrowseCountersHA(PPDH_BROWSE_DLG_CONFIG_HA pBrowseDlgData);
PDH_STATUS WINAPI PdhBrowseCountersHW(PPDH_BROWSE_DLG_CONFIG_HW pBrowseDlgData);
PDH_STATUS WINAPI PdhBrowseCountersW(PPDH_BROWSE_DLG_CONFIG_W pBrowseDlgData);
PDH_STATUS WINAPI PdhCloseLog(PDH_HLOG hLog, DWORD dwFlags);
PDH_STATUS WINAPI PdhComputeCounterStatistics(PDH_HCOUNTER hCounter, DWORD dwFormat, DWORD dwFirstEntry, DWORD dwNumEntries, PPDH_RAW_COUNTER lpRawValueArray, PPDH_STATISTICS data);
PDH_STATUS WINAPI PdhCreateSQLTablesA(LPCSTR szDataSource);
PDH_STATUS WINAPI PdhCreateSQLTablesW(LPCWSTR szDataSource);
PDH_STATUS WINAPI PdhEnumLogSetNamesA(LPCSTR szDataSource, PZZSTR mszDataSetNameList, LPDWORD pcchBufferLength);
PDH_STATUS WINAPI PdhEnumLogSetNamesW(LPCWSTR szDataSource, PZZWSTR mszDataSetNameList, LPDWORD pcchBufferLength);
PDH_STATUS WINAPI PdhEnumMachinesHA(PDH_HLOG hDataSource, PZZSTR mszMachineList, LPDWORD pcchBufferSize);
PDH_STATUS WINAPI PdhEnumMachinesHW(PDH_HLOG hDataSource, PZZWSTR mszMachineList, LPDWORD pcchBufferSize);
PDH_STATUS WINAPI PdhEnumObjectItemsHA(PDH_HLOG hDataSource, LPCSTR szMachineName, LPCSTR szObjectName, PZZSTR mszCounterList, LPDWORD pcchCounterListLength, PZZSTR mszInstanceList, LPDWORD pcchInstanceListLength, DWORD dwDetailLevel, DWORD dwFlags);
PDH_STATUS WINAPI PdhEnumObjectItemsHW(PDH_HLOG hDataSource, LPCWSTR szMachineName, LPCWSTR szObjectName, PZZWSTR mszCounterList, LPDWORD pcchCounterListLength, PZZWSTR mszInstanceList, LPDWORD pcchInstanceListLength, DWORD dwDetailLevel, DWORD dwFlags);
PDH_STATUS WINAPI PdhEnumObjectsHA(PDH_HLOG hDataSource, LPCSTR szMachineName, PZZSTR mszObjectList, LPDWORD pcchBufferSize, DWORD dwDetailLevel, BOOL bRefresh);
PDH_STATUS WINAPI PdhEnumObjectsHW(PDH_HLOG hDataSource, LPCWSTR szMachineName, PZZWSTR mszObjectList, LPDWORD pcchBufferSize, DWORD dwDetailLevel, BOOL bRefresh);
PDH_STATUS WINAPI PdhExpandWildCardPathHA(PDH_HLOG hDataSource, LPCSTR szWildCardPath, PZZSTR mszExpandedPathList, LPDWORD pcchPathListLength, DWORD dwFlags);
PDH_STATUS WINAPI PdhExpandWildCardPathHW(PDH_HLOG hDataSource, LPCWSTR szWildCardPath, PZZWSTR mszExpandedPathList, LPDWORD pcchPathListLength, DWORD dwFlags);
PDH_STATUS WINAPI PdhFormatFromRawValue(DWORD dwCounterType, DWORD dwFormat, LONGLONG *pTimeBase, PPDH_RAW_COUNTER pRawValue1, PPDH_RAW_COUNTER pRawValue2, PPDH_FMT_COUNTERVALUE pFmtValue);
PDH_STATUS WINAPI PdhGetDataSourceTimeRangeA(LPCSTR szDataSource, LPDWORD pdwNumEntries, PPDH_TIME_INFO pInfo, LPDWORD pdwBufferSize);
PDH_STATUS WINAPI PdhGetDataSourceTimeRangeH(PDH_HLOG hDataSource, LPDWORD pdwNumEntries, PPDH_TIME_INFO pInfo, LPDWORD pdwBufferSize);
PDH_STATUS WINAPI PdhGetDataSourceTimeRangeW(LPCWSTR szDataSource, LPDWORD pdwNumEntries, PPDH_TIME_INFO pInfo, LPDWORD pdwBufferSize);
PDH_STATUS WINAPI PdhGetDefaultPerfCounterA(LPCSTR szDataSource, LPCSTR szMachineName, LPCSTR szObjectName, LPSTR szDefaultCounterName, LPDWORD pcchBufferSize);
PDH_STATUS WINAPI PdhGetDefaultPerfCounterHA(PDH_HLOG hDataSource, LPCSTR szMachineName, LPCSTR szObjectName, LPSTR szDefaultCounterName, LPDWORD pcchBufferSize);
PDH_STATUS WINAPI PdhGetDefaultPerfCounterHW(PDH_HLOG hDataSource, LPCWSTR szMachineName, LPCWSTR szObjectName, LPWSTR szDefaultCounterName, LPDWORD pcchBufferSize);
PDH_STATUS WINAPI PdhGetDefaultPerfCounterW(LPCWSTR szDataSource, LPCWSTR szMachineName, LPCWSTR szObjectName, LPWSTR szDefaultCounterName, LPDWORD pcchBufferSize);
PDH_STATUS WINAPI PdhGetDefaultPerfObjectA(LPCSTR szDataSource, LPCSTR szMachineName, LPSTR szDefaultObjectName, LPDWORD pcchBufferSize);
PDH_STATUS WINAPI PdhGetDefaultPerfObjectHA(PDH_HLOG hDataSource, LPCSTR szMachineName, LPSTR szDefaultObjectName, LPDWORD pcchBufferSize);
PDH_STATUS WINAPI PdhGetDefaultPerfObjectHW(PDH_HLOG hDataSource, LPCWSTR szMachineName, LPWSTR szDefaultObjectName, LPDWORD pcchBufferSize);
PDH_STATUS WINAPI PdhGetDefaultPerfObjectW(LPCWSTR szDataSource, LPCWSTR szMachineName, LPWSTR szDefaultObjectName, LPDWORD pcchBufferSize);
PDH_STATUS WINAPI PdhGetLogFileSize(PDH_HLOG hLog, LONGLONG *llSize);
PDH_STATUS WINAPI PdhGetLogSetGUID(PDH_HLOG hLog, GUID *pGuid, int *pRunId);
BOOL WINAPI PdhIsRealTimeQuery(PDH_HQUERY hQuery);
PDH_STATUS WINAPI PdhOpenLogA(LPCSTR szLogFileName, DWORD dwAccessFlags, LPDWORD lpdwLogType, PDH_HQUERY hQuery, DWORD dwMaxSize, LPCSTR szUserCaption, PDH_HLOG *phLog);
PDH_STATUS WINAPI PdhOpenLogW(LPCWSTR szLogFileName, DWORD dwAccessFlags, LPDWORD lpdwLogType, PDH_HQUERY hQuery, DWORD dwMaxSize, LPCWSTR szUserCaption, PDH_HLOG *phLog);
PDH_STATUS WINAPI PdhOpenQueryH(PDH_HLOG hDataSource, DWORD_PTR dwUserData, PDH_HQUERY *phQuery);
PDH_STATUS WINAPI PdhParseCounterPathA(LPCSTR szFullPathBuffer, PPDH_COUNTER_PATH_ELEMENTS_A pCounterPathElements, LPDWORD pdwBufferSize, DWORD dwFlags);
PDH_STATUS WINAPI PdhParseCounterPathW(LPCWSTR szFullPathBuffer, PPDH_COUNTER_PATH_ELEMENTS_W pCounterPathElements, LPDWORD pdwBufferSize, DWORD dwFlags);
PDH_STATUS WINAPI PdhParseInstanceNameA(LPCSTR szInstanceString, LPSTR szInstanceName, LPDWORD pcchInstanceNameLength, LPSTR szParentName, LPDWORD pcchParentNameLength, LPDWORD lpIndex);
PDH_STATUS WINAPI PdhParseInstanceNameW(LPCWSTR szInstanceString, LPWSTR szInstanceName, LPDWORD pcchInstanceNameLength, LPWSTR szParentName, LPDWORD pcchParentNameLength, LPDWORD lpIndex);
PDH_STATUS WINAPI PdhReadRawLogRecord(PDH_HLOG hLog, FILETIME ftRecord, PPDH_RAW_LOG_RECORD pRawLogRecord, LPDWORD pdwBufferLength);
PDH_STATUS WINAPI PdhSelectDataSourceA(HWND hWndOwner, DWORD dwFlags, LPSTR szDataSource, LPDWORD pcchBufferLength);
PDH_STATUS WINAPI PdhSelectDataSourceW(HWND hWndOwner, DWORD dwFlags, LPWSTR szDataSource, LPDWORD pcchBufferLength);
PDH_STATUS WINAPI PdhSetLogSetRunID(PDH_HLOG hLog, int RunId);
PDH_STATUS WINAPI PdhSetQueryTimeRange(PDH_HQUERY hQuery, PPDH_TIME_INFO pInfo);
PDH_STATUS WINAPI PdhUpdateLogA(PDH_HLOG hLog, LPCSTR szUserString);
PDH_STATUS WINAPI PdhUpdateLogFileCatalog(PDH_HLOG hLog);
PDH_STATUS WINAPI PdhUpdateLogW(PDH_HLOG hLog, LPCWSTR szUserString);
PDH_STATUS WINAPI PdhVerifySQLDBA(LPCSTR szDataSource);
PDH_STATUS WINAPI PdhVerifySQLDBW(LPCWSTR szDataSource);

#ifdef __cplusplus
}
#endif

#define PdhLogServiceControl 0

#define PdhLogServiceQuery 0


#define PDH_BROWSE_DLG_CONFIG_H 0
#ifdef UNICODE
#define PPDH_BROWSE_DLG_CONFIG_H PPDH_BROWSE_DLG_CONFIG_HW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define PPDH_BROWSE_DLG_CONFIG_H PPDH_BROWSE_DLG_CONFIG_HW
#else
#define PPDH_BROWSE_DLG_CONFIG_H PPDH_BROWSE_DLG_CONFIG_HA
#endif
#endif
#endif

#define IsErrorSeverity(ErrorCode) ((((DWORD) (ErrorCode) & (0xC0000000L)) == 0xC0000000L) ? TRUE : FALSE)
#define IsInformationalSeverity(ErrorCode) ((((DWORD) (ErrorCode) & (0xC0000000L)) == 0x40000000L) ? TRUE : FALSE)
#define IsSuccessSeverity(ErrorCode) ((((DWORD) (ErrorCode) & (0xC0000000L)) == 0x00000000L) ? TRUE : FALSE)
#define IsWarningSeverity(ErrorCode) ((((DWORD) (ErrorCode) & (0xC0000000L)) == 0x80000000L) ? TRUE : FALSE)
#define PDH_PATH_LANG_FLAGS(LangId, Flags) ((DWORD) (((LangId & 0x0000FFFF) << 16) | (Flags & 0x0000FFFF)))

#endif /* _PDH_H_ */
