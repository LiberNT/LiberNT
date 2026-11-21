/* Definitions for the Windows Event Log (wevtapi.dll)
 *
 * Copyright 2012 André Hentschel
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

#ifndef __WINE_WINEVT_H
#define __WINE_WINEVT_H

#include <wtypes.h>

#ifdef __cplusplus
extern "C" {
#endif /* defined(__cplusplus) */

typedef HANDLE EVT_HANDLE, *PEVT_HANDLE;

typedef enum _EVT_CHANNEL_CONFIG_PROPERTY_ID {
    EvtChannelConfigEnabled = 0,
    EvtChannelConfigIsolation,
    EvtChannelConfigType,
    EvtChannelConfigOwningPublisher,
    EvtChannelConfigClassicEventlog,
    EvtChannelConfigAccess,
    EvtChannelLoggingConfigRetention,
    EvtChannelLoggingConfigAutoBackup,
    EvtChannelLoggingConfigMaxSize,
    EvtChannelLoggingConfigLogFilePath,
    EvtChannelPublishingConfigLevel,
    EvtChannelPublishingConfigKeywords,
    EvtChannelPublishingConfigControlGuid,
    EvtChannelPublishingConfigBufferSize,
    EvtChannelPublishingConfigMinBuffers,
    EvtChannelPublishingConfigMaxBuffers,
    EvtChannelPublishingConfigLatency,
    EvtChannelPublishingConfigClockType,
    EvtChannelPublishingConfigSidType,
    EvtChannelPublisherList,
    EvtChannelPublishingConfigFileMax,
    EvtChannelConfigPropertyIdEND
} EVT_CHANNEL_CONFIG_PROPERTY_ID;

typedef enum _EVT_LOGIN_CLASS {
    EvtRpcLogin = 1
} EVT_LOGIN_CLASS;

typedef enum _EVT_SUBSCRIBE_NOTIFY_ACTION {
    EvtSubscribeActionError = 0,
    EvtSubscribeActionDeliver
} EVT_SUBSCRIBE_NOTIFY_ACTION;

typedef enum _EVT_VARIANT_TYPE {
    EvtVarTypeNull,
    EvtVarTypeString,
    EvtVarTypeAnsiString,
    EvtVarTypeSByte,
    EvtVarTypeByte,
    EvtVarTypeInt16,
    EvtVarTypeUInt16,
    EvtVarTypeInt32,
    EvtVarTypeUInt32,
    EvtVarTypeInt64,
    EvtVarTypeUInt64,
    EvtVarTypeSingle,
    EvtVarTypeDouble,
    EvtVarTypeBoolean,
    EvtVarTypeBinary,
    EvtVarTypeGuid,
    EvtVarTypeSizeT,
    EvtVarTypeFileTime,
    EvtVarTypeSysTime,
    EvtVarTypeSid,
    EvtVarTypeHexInt32,
    EvtVarTypeHexInt64,
    EvtVarTypeEvtHandle = 32,
    EvtVarTypeEvtXml = 35
} EVT_VARIANT_TYPE;

#define EVT_VARIANT_TYPE_MASK  0x7f
#define EVT_VARIANT_TYPE_ARRAY 128

typedef struct _EVT_VARIANT {
    union {
        BOOL        BooleanVal;
        INT8        SByteVal;
        INT16       Int16Val;
        INT32       Int32Val;
        INT64       Int64Val;
        UINT8       ByteVal;
        UINT16      UInt16Val;
        UINT32      UInt32Val;
        UINT64      UInt64Val;
        FLOAT       SingleVal;
        DOUBLE      DoubleVal;
        ULONGLONG   FileTimeVal;
        SYSTEMTIME* SysTimeVal;
        GUID*       GuidVal;
        LPCWSTR     StringVal;
        LPCSTR      AnsiStringVal;
        PBYTE       BinaryVal;
        PSID        SidVal;
        SIZE_T      SizeTVal;

        BOOL*       BooleanArr;
        INT8*       SByteArr;
        INT16*      Int16Arr;
        INT32*      Int32Arr;
        INT64*      Int64Arr;
        UINT8*      ByteArr;
        UINT16*     UInt16Arr;
        UINT32*     UInt32Arr;
        UINT64*     UInt64Arr;
        FLOAT*      SingleArr;
        DOUBLE*     DoubleArr;
        FILETIME*   FileTimeArr;
        SYSTEMTIME* SysTimeArr;
        GUID*       GuidArr;
        LPWSTR*     StringArr;
        LPSTR*      AnsiStringArr;
        PSID*       SidArr;
        SIZE_T*     SizeTArr;

        EVT_HANDLE  EvtHandleVal;
        LPCWSTR     XmlVal;
        LPCWSTR*    XmlValArr;
    } DUMMYUNIONNAME;

    DWORD Count;
    DWORD Type;
} EVT_VARIANT, *PEVT_VARIANT;

typedef DWORD (WINAPI *EVT_SUBSCRIBE_CALLBACK)(EVT_SUBSCRIBE_NOTIFY_ACTION Action,
                                               PVOID UserContext, EVT_HANDLE Event);
EVT_HANDLE WINAPI EvtCreateBookmark(LPCWSTR);
EVT_HANDLE WINAPI EvtCreateRenderContext(DWORD,LPCWSTR*,DWORD);
BOOL WINAPI EvtClose(EVT_HANDLE);
BOOL WINAPI EvtExportLog(EVT_HANDLE session, const WCHAR *path, const WCHAR *query,
                         const WCHAR *file, DWORD flags);
BOOL WINAPI EvtGetChannelConfigProperty(EVT_HANDLE ChannelConfig,
                                        EVT_CHANNEL_CONFIG_PROPERTY_ID PropertyId,
                                        DWORD Flags, DWORD PropertyValueBufferSize,
                                        PEVT_VARIANT PropertyValueBuffer,
                                        PDWORD PropertyValueBufferUsed);
BOOL WINAPI EvtSetChannelConfigProperty(EVT_HANDLE ChannelConfig,
                                        EVT_CHANNEL_CONFIG_PROPERTY_ID PropertyId,
                                        DWORD Flags, PEVT_VARIANT PropertyValue);
EVT_HANDLE WINAPI EvtSubscribe(EVT_HANDLE Session, HANDLE SignalEvent, LPCWSTR ChannelPath,
                               LPCWSTR Query, EVT_HANDLE Bookmark, PVOID context,
                               EVT_SUBSCRIBE_CALLBACK Callback, DWORD Flags);
EVT_HANDLE WINAPI EvtOpenChannelConfig(EVT_HANDLE Session, LPCWSTR ChannelPath, DWORD Flags);
BOOL WINAPI EvtSaveChannelConfig(EVT_HANDLE,DWORD);
BOOL WINAPI EvtRender(EVT_HANDLE, EVT_HANDLE, DWORD, DWORD, void *, DWORD *, DWORD *);

#ifdef __cplusplus
} /* extern "C" */
#endif /* defined(__cplusplus) */

#ifndef EVT_ALL_ACCESS
#define EVT_ALL_ACCESS 0x7
#endif
#ifndef EVT_CLEAR_ACCESS
#define EVT_CLEAR_ACCESS 0x4
#endif
#ifndef EVT_READ_ACCESS
#define EVT_READ_ACCESS 0x1
#endif
#ifndef EVT_WRITE_ACCESS
#define EVT_WRITE_ACCESS 0x2
#endif

typedef enum _EVT_CHANNEL_CLOCK_TYPE {
    EvtChannelClockTypeSystemTime = 0,
    EvtChannelClockTypeQPC = 1
} EVT_CHANNEL_CLOCK_TYPE;

typedef enum _EVT_CHANNEL_ISOLATION_TYPE {
    EvtChannelIsolationTypeApplication = 0,
    EvtChannelIsolationTypeSystem = 1,
    EvtChannelIsolationTypeCustom = 2
} EVT_CHANNEL_ISOLATION_TYPE;

typedef enum _EVT_CHANNEL_REFERENCE_FLAGS {
    EvtChannelReferenceImported = 1
} EVT_CHANNEL_REFERENCE_FLAGS;

typedef enum _EVT_CHANNEL_SID_TYPE {
    EvtChannelSidTypeNone = 0,
    EvtChannelSidTypePublishing = 1
} EVT_CHANNEL_SID_TYPE;

typedef enum _EVT_CHANNEL_TYPE {
    EvtChannelTypeAdmin = 0,
    EvtChannelTypeOperational = 1,
    EvtChannelTypeAnalytic = 2,
    EvtChannelTypeDebug = 3
} EVT_CHANNEL_TYPE;

typedef enum _EVT_EVENT_METADATA_PROPERTY_ID {
    EventMetadataEventID = 0,
    EventMetadataEventVersion = 1,
    EventMetadataEventChannel = 2,
    EventMetadataEventLevel = 3,
    EventMetadataEventOpcode = 4,
    EventMetadataEventTask = 5,
    EventMetadataEventKeyword = 6,
    EventMetadataEventMessageID = 7,
    EventMetadataEventTemplate = 8,
    EvtEventMetadataPropertyIdEND = 9
} EVT_EVENT_METADATA_PROPERTY_ID;

typedef enum _EVT_EVENT_PROPERTY_ID {
    EvtEventQueryIDs = 0,
    EvtEventPath = 1,
    EvtEventPropertyIdEND = 2
} EVT_EVENT_PROPERTY_ID;

typedef enum _EVT_EXPORTLOG_FLAGS {
    EvtExportLogChannelPath = 1,
    EvtExportLogFilePath = 2,
    EvtExportLogTolerateQueryErrors = 0x1000,
    EvtExportLogOverwrite = 0x2000
} EVT_EXPORTLOG_FLAGS;

typedef enum _EVT_FORMAT_MESSAGE_FLAGS {
    EvtFormatMessageEvent = 1,
    EvtFormatMessageLevel = 2,
    EvtFormatMessageTask = 3,
    EvtFormatMessageOpcode = 4,
    EvtFormatMessageKeyword = 5,
    EvtFormatMessageChannel = 6,
    EvtFormatMessageProvider = 7,
    EvtFormatMessageId = 8,
    EvtFormatMessageXml = 9
} EVT_FORMAT_MESSAGE_FLAGS;

typedef enum _EVT_LOG_PROPERTY_ID {
    EvtLogCreationTime = 0,
    EvtLogLastAccessTime = 1,
    EvtLogLastWriteTime = 2,
    EvtLogFileSize = 3,
    EvtLogAttributes = 4,
    EvtLogNumberOfLogRecords = 5,
    EvtLogOldestRecordNumber = 6,
    EvtLogFull = 7
} EVT_LOG_PROPERTY_ID;

typedef enum _EVT_OPEN_LOG_FLAGS {
    EvtOpenChannelPath = 1,
    EvtOpenFilePath = 2
} EVT_OPEN_LOG_FLAGS;

typedef enum _EVT_PUBLISHER_METADATA_PROPERTY_ID {
    EvtPublisherMetadataPublisherGuid = 0,
    EvtPublisherMetadataResourceFilePath = 1,
    EvtPublisherMetadataParameterFilePath = 2,
    EvtPublisherMetadataMessageFilePath = 3,
    EvtPublisherMetadataHelpLink = 4,
    EvtPublisherMetadataPublisherMessageID = 5,
    EvtPublisherMetadataChannelReferences = 6,
    EvtPublisherMetadataChannelReferencePath = 7,
    EvtPublisherMetadataChannelReferenceIndex = 8,
    EvtPublisherMetadataChannelReferenceID = 9,
    EvtPublisherMetadataChannelReferenceFlags = 10,
    EvtPublisherMetadataChannelReferenceMessageID = 11,
    EvtPublisherMetadataLevels = 12,
    EvtPublisherMetadataLevelName = 13,
    EvtPublisherMetadataLevelValue = 14,
    EvtPublisherMetadataLevelMessageID = 15,
    EvtPublisherMetadataTasks = 16,
    EvtPublisherMetadataTaskName = 17,
    EvtPublisherMetadataTaskEventGuid = 18,
    EvtPublisherMetadataTaskValue = 19,
    EvtPublisherMetadataTaskMessageID = 20,
    EvtPublisherMetadataOpcodes = 21,
    EvtPublisherMetadataOpcodeName = 22,
    EvtPublisherMetadataOpcodeValue = 23,
    EvtPublisherMetadataOpcodeMessageID = 24,
    EvtPublisherMetadataKeywords = 25,
    EvtPublisherMetadataKeywordName = 26,
    EvtPublisherMetadataKeywordValue = 27,
    EvtPublisherMetadataKeywordMessageID = 28,
    EvtPublisherMetadataPropertyIdEND = 29
} EVT_PUBLISHER_METADATA_PROPERTY_ID;

typedef enum _EVT_QUERY_FLAGS {
    EvtQueryChannelPath = 1,
    EvtQueryFilePath = 2,
    EvtQueryForwardDirection = 0x100,
    EvtQueryReverseDirection = 0x200,
    EvtQueryTolerateQueryErrors = 0x1000
} EVT_QUERY_FLAGS;

typedef enum _EVT_QUERY_PROPERTY_ID {
    EvtQueryNames = 0,
    EvtQueryStatuses = 1,
    EvtQueryPropertyIdEND = 2
} EVT_QUERY_PROPERTY_ID;

typedef enum _EVT_RENDER_CONTEXT_FLAGS {
    EvtRenderContextValues = 0,
    EvtRenderContextSystem = 1,
    EvtRenderContextUser = 2
} EVT_RENDER_CONTEXT_FLAGS;

typedef enum _EVT_RENDER_FLAGS {
    EvtRenderEventValues = 0,
    EvtRenderEventXml = 1,
    EvtRenderBookmark = 2
} EVT_RENDER_FLAGS;

typedef enum _EVT_RPC_LOGIN_FLAGS {
    EvtRpcLoginAuthDefault = 0,
    EvtRpcLoginAuthNegotiate = 1,
    EvtRpcLoginAuthKerberos = 2,
    EvtRpcLoginAuthNTLM = 3
} EVT_RPC_LOGIN_FLAGS;

typedef enum _EVT_SEEK_FLAGS {
    EvtSeekRelativeToFirst = 1,
    EvtSeekRelativeToLast = 2,
    EvtSeekRelativeToCurrent = 3,
    EvtSeekRelativeToBookmark = 4,
    EvtSeekOriginMask = 7,
    EvtSeekStrict = 0x10000
} EVT_SEEK_FLAGS;

typedef enum _EVT_SUBSCRIBE_FLAGS {
    EvtSubscribeToFutureEvents = 1,
    EvtSubscribeStartAtOldestRecord = 2,
    EvtSubscribeStartAfterBookmark = 3,
    EvtSubscribeOriginMask = 3,
    EvtSubscribeTolerateQueryErrors = 0x1000,
    EvtSubscribeStrict = 0x10000
} EVT_SUBSCRIBE_FLAGS;

typedef enum _EVT_SYSTEM_PROPERTY_ID {
    EvtSystemProviderName = 0,
    EvtSystemProviderGuid = 1,
    EvtSystemEventID = 2,
    EvtSystemQualifiers = 3,
    EvtSystemLevel = 4,
    EvtSystemTask = 5,
    EvtSystemOpcode = 6,
    EvtSystemKeywords = 7,
    EvtSystemTimeCreated = 8,
    EvtSystemEventRecordId = 9,
    EvtSystemActivityID = 10,
    EvtSystemRelatedActivityID = 11,
    EvtSystemProcessID = 12,
    EvtSystemThreadID = 13,
    EvtSystemChannel = 14,
    EvtSystemComputer = 15,
    EvtSystemUserID = 16,
    EvtSystemVersion = 17,
    EvtSystemPropertyIdEND = 18
} EVT_SYSTEM_PROPERTY_ID;

typedef HANDLE EVT_OBJECT_ARRAY_PROPERTY_HANDLE;

#ifdef __cplusplus
extern "C" {
#endif
EVT_HANDLE WINAPI EvtOpenChannelEnum(EVT_HANDLE Session, DWORD Flags);
EVT_HANDLE WINAPI EvtOpenLog(EVT_HANDLE Session, LPCWSTR Path, DWORD Flags);
EVT_HANDLE WINAPI EvtQuery(EVT_HANDLE Session, LPCWSTR Path, LPCWSTR Query, DWORD Flags);
#ifdef __cplusplus
}
#endif

typedef struct _EVT_RPC_LOGIN {
  LPWSTR Server;
  LPWSTR User;
  LPWSTR Domain;
  LPWSTR Password;
  DWORD Flags;
} EVT_RPC_LOGIN;

#ifdef __cplusplus
extern "C" {
#endif

BOOL WINAPI EvtNext(EVT_HANDLE ResultSet, DWORD EventsSize, PEVT_HANDLE Events, DWORD Timeout, DWORD Flags, PDWORD Returned);
BOOL WINAPI EvtNextChannelPath(EVT_HANDLE ChannelEnum, DWORD ChannelPathBufferSize, LPWSTR ChannelPathBuffer, PDWORD ChannelPathBufferUsed);
EVT_HANDLE WINAPI EvtOpenSession(EVT_LOGIN_CLASS LoginClass, PVOID Login, DWORD Timeout, DWORD Flags);

/* TODO: LiberNT does not implement the functions below yet */
BOOL WINAPI EvtArchiveExportedLog(EVT_HANDLE Session, LPCWSTR LogFilePath, LCID Locale, DWORD Flags);
BOOL WINAPI EvtClearLog(EVT_HANDLE Session, LPCWSTR ChannelPath, LPCWSTR TargetFilePath, DWORD Flags);
BOOL WINAPI EvtFormatMessage(EVT_HANDLE PublisherMetadata, EVT_HANDLE Event, DWORD MessageId, DWORD ValueCount, PEVT_VARIANT Values, DWORD Flags, DWORD BufferSize, LPWSTR Buffer, PDWORD BufferUsed);
BOOL WINAPI EvtGetEventInfo(EVT_HANDLE Event, EVT_EVENT_PROPERTY_ID PropertyId, DWORD PropertyValueBufferSize, PEVT_VARIANT PropertyValueBuffer, PDWORD PropertyValueBufferUsed);
BOOL WINAPI EvtGetEventMetadataProperty(EVT_HANDLE EventMetadata, EVT_EVENT_METADATA_PROPERTY_ID PropertyId, DWORD Flags, DWORD EventMetadataPropertyBufferSize, PEVT_VARIANT EventMetadataPropertyBuffer, PDWORD EventMetadataPropertyBufferUsed);
DWORD WINAPI EvtGetExtendedStatus(DWORD BufferSize, LPWSTR Buffer, PDWORD BufferUsed);
BOOL WINAPI EvtGetLogInfo(EVT_HANDLE Log, EVT_LOG_PROPERTY_ID PropertyId, DWORD PropertyValueBufferSize, PEVT_VARIANT PropertyValueBuffer, PDWORD PropertyValueBufferUsed);
BOOL WINAPI EvtGetObjectArrayProperty(EVT_OBJECT_ARRAY_PROPERTY_HANDLE ObjectArray, DWORD PropertyId, DWORD ArrayIndex, DWORD Flags, DWORD PropertyValueBufferSize, PEVT_VARIANT PropertyValueBuffer, PDWORD PropertyValueBufferUsed);
BOOL WINAPI EvtGetObjectArraySize(EVT_OBJECT_ARRAY_PROPERTY_HANDLE ObjectArray, PDWORD ObjectArraySize);
BOOL WINAPI EvtGetPublisherMetadataProperty(EVT_HANDLE PublisherMetadata, EVT_PUBLISHER_METADATA_PROPERTY_ID PropertyId, DWORD Flags, DWORD PublisherMetadataPropertyBufferSize, PEVT_VARIANT PublisherMetadataPropertyBuffer, PDWORD PublisherMetadataPropertyBufferUsed);
BOOL WINAPI EvtGetQueryInfo(EVT_HANDLE QueryOrSubscription, EVT_QUERY_PROPERTY_ID PropertyId, DWORD PropertyValueBufferSize, PEVT_VARIANT PropertyValueBuffer, PDWORD PropertyValueBufferUsed);
EVT_HANDLE WINAPI EvtNextEventMetadata(EVT_HANDLE EventMetadataEnum, DWORD Flags);
BOOL WINAPI EvtNextPublisherId(EVT_HANDLE PublisherEnum, DWORD PublisherIdBufferSize, LPWSTR PublisherIdBuffer, PDWORD PublisherIdBufferUsed);
EVT_HANDLE WINAPI EvtOpenEventMetadataEnum(EVT_HANDLE PublisherMetadata, DWORD Flags);
EVT_HANDLE WINAPI EvtOpenPublisherEnum(EVT_HANDLE Session, DWORD Flags);
EVT_HANDLE WINAPI EvtOpenPublisherMetadata(EVT_HANDLE Session, LPCWSTR PublisherId, LPCWSTR LogFilePath, LCID Locale, DWORD Flags);
BOOL WINAPI EvtSeek(EVT_HANDLE ResultSet, LONGLONG Position, EVT_HANDLE Bookmark, DWORD Timeout, DWORD Flags);
BOOL WINAPI EvtUpdateBookmark(EVT_HANDLE Bookmark, EVT_HANDLE Event);

#ifdef __cplusplus
}
#endif

#endif /* __WINE_WINEVT_H */
