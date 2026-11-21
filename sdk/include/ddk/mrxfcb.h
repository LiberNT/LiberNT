#ifndef __MRXFCB_H__
#define __MRXFCB_H__

typedef struct _MRX_NORMAL_NODE_HEADER
{
    NODE_TYPE_CODE NodeTypeCode;
    NODE_BYTE_SIZE NodeByteSize;
    volatile ULONG NodeReferenceCount;
} MRX_NORMAL_NODE_HEADER;

#define SRVCALL_FLAG_CASE_INSENSITIVE_NETROOTS 0x4
#define SRVCALL_FLAG_CASE_INSENSITIVE_FILENAMES 0x8
#define SRVCALL_FLAG_DFS_AWARE_SERVER 0x10
#define SRVCALL_FLAG_FORCE_FINALIZED 0x20

typedef struct _MRX_SRV_CALL_
{
    MRX_NORMAL_NODE_HEADER;
    PVOID Context;
    PVOID Context2;
    PRDBSS_DEVICE_OBJECT RxDeviceObject;
    PUNICODE_STRING pSrvCallName;
    PUNICODE_STRING pPrincipalName;
    PUNICODE_STRING pDomainName;
    ULONG Flags;
    LONG MaximumNumberOfCloseDelayedFiles;
    NTSTATUS Status;
} MRX_SRV_CALL, *PMRX_SRV_CALL;

#define NET_ROOT_DISK ((UCHAR)0)
#define NET_ROOT_PIPE ((UCHAR)1)
#define NET_ROOT_PRINT ((UCHAR)3)
#define NET_ROOT_WILD ((UCHAR)4)
#define NET_ROOT_MAILSLOT ((UCHAR)5)

typedef UCHAR NET_ROOT_TYPE, *PNET_ROOT_TYPE;

#define MRX_NET_ROOT_STATE_GOOD ((UCHAR)0)

typedef UCHAR MRX_NET_ROOT_STATE, *PMRX_NET_ROOT_STATE;
typedef UCHAR MRX_PURGE_RELATIONSHIP, *PMRX_PURGE_RELATIONSHIP;
typedef UCHAR MRX_PURGE_SYNCLOCATION, *PMRX_PURGE_SYNCLOCATION;

#define NETROOT_FLAG_SUPPORTS_SYMBOLIC_LINKS 0x1
#define NETROOT_FLAG_DFS_AWARE_NETROOT 0x2

typedef struct _NETROOT_THROTTLING_PARAMETERS
{
    ULONG Increment;
    ULONG MaximumDelay;
} NETROOT_THROTTLING_PARAMETERS, *PNETROOT_THROTTLING_PARAMETERS;

typedef struct _MRX_NET_ROOT_
{
    MRX_NORMAL_NODE_HEADER;
    PMRX_SRV_CALL pSrvCall;
    PVOID Context;
    PVOID Context2;
    ULONG Flags;
    volatile ULONG NumberOfFcbs;
    volatile ULONG NumberOfSrvOpens;
    MRX_NET_ROOT_STATE MRxNetRootState;
    NET_ROOT_TYPE Type;
    MRX_PURGE_RELATIONSHIP PurgeRelationship;
    MRX_PURGE_SYNCLOCATION PurgeSyncLocation;
    DEVICE_TYPE DeviceType;
    PUNICODE_STRING pNetRootName;
    UNICODE_STRING InnerNamePrefix;
    ULONG  ParameterValidationStamp;
    union
    {
        struct
        {
            ULONG DataCollectionSize;
            NETROOT_THROTTLING_PARAMETERS PipeReadThrottlingParameters;
        } NamedPipeParameters;
        struct
        {
            ULONG ClusterSize;
            ULONG ReadAheadGranularity;
            NETROOT_THROTTLING_PARAMETERS LockThrottlingParameters;
            ULONG RenameInfoOverallocationSize;
            GUID VolumeId;
        } DiskParameters;
    };
} MRX_NET_ROOT, *PMRX_NET_ROOT;

#define VNETROOT_FLAG_CSCAGENT_INSTANCE 0x00000001

typedef struct _MRX_V_NET_ROOT_
{
    MRX_NORMAL_NODE_HEADER;
    PMRX_NET_ROOT pNetRoot;
    PVOID Context;
    PVOID Context2;
    ULONG Flags;
    ULONG NumberOfOpens;
    volatile ULONG NumberOfFobxs;
    LUID LogonId;
    PUNICODE_STRING pUserDomainName;
    PUNICODE_STRING pUserName;
    PUNICODE_STRING pPassword;
    ULONG SessionId;
    NTSTATUS ConstructionStatus;
    BOOLEAN IsExplicitConnection;
} MRX_V_NET_ROOT, *PMRX_V_NET_ROOT;

typedef struct _MRX_FCB_
{
    FSRTL_ADVANCED_FCB_HEADER Header;
    PMRX_NET_ROOT pNetRoot;
    PVOID Context;
    PVOID Context2;
    volatile ULONG NodeReferenceCount;
    ULONG FcbState;
    volatile CLONG UncleanCount;
    CLONG UncachedUncleanCount;
    volatile CLONG OpenCount;
    volatile ULONG OutstandingLockOperationsCount;
    ULONGLONG ActualAllocationLength;
    ULONG Attributes;
    BOOLEAN IsFileWritten;
    BOOLEAN fShouldBeOrphaned;
    BOOLEAN fMiniInited;
    UCHAR CachedNetRootType;
    LIST_ENTRY SrvOpenList;
    ULONG SrvOpenListVersion;
} MRX_FCB, *PMRX_FCB;

#define SRVOPEN_FLAG_DONTUSE_READ_CACHING 0x1
#define SRVOPEN_FLAG_DONTUSE_WRITE_CACHING 0x2
#define SRVOPEN_FLAG_CLOSED 0x4
#define SRVOPEN_FLAG_CLOSE_DELAYED 0x8
#define SRVOPEN_FLAG_FILE_RENAMED 0x10
#define SRVOPEN_FLAG_FILE_DELETED 0x20
#define SRVOPEN_FLAG_BUFFERING_STATE_CHANGE_PENDING 0x40
#define SRVOPEN_FLAG_COLLAPSING_DISABLED 0x80
#define SRVOPEN_FLAG_BUFFERING_STATE_CHANGE_REQUESTS_PURGED 0x100
#define SRVOPEN_FLAG_NO_BUFFERING_STATE_CHANGE 0x200
#define SRVOPEN_FLAG_ORPHANED 0x400

typedef
NTSTATUS
(NTAPI *PMRX_SHADOW_CALLDOWN) (
    IN OUT PRX_CONTEXT RxContext
    );

typedef struct
{
    PFILE_OBJECT UnderlyingFileObject;
    PDEVICE_OBJECT UnderlyingDeviceObject;
    ULONG LockKey;
    PFAST_IO_READ FastIoRead;
    PFAST_IO_WRITE FastIoWrite;
    PMRX_SHADOW_CALLDOWN DispatchRoutine;
} MRXSHADOW_SRV_OPEN, *PMRXSHADOW_SRV_OPEN;

typedef struct _MRX_SRV_OPEN_
{
    MRX_NORMAL_NODE_HEADER;
    PMRX_FCB pFcb;
    PMRX_V_NET_ROOT pVNetRoot;
    PVOID Context;
    PVOID Context2;
#if (_WIN32_WINNT >= 0x0600)
    PMRXSHADOW_SRV_OPEN ShadowContext;
#endif
    ULONG Flags;
    PUNICODE_STRING pAlreadyPrefixedName;
    CLONG UncleanFobxCount;
    CLONG OpenCount;
    PVOID Key;
    ACCESS_MASK DesiredAccess;
    ULONG ShareAccess;
    ULONG CreateOptions;
    ULONG BufferingFlags;
    ULONG ulFileSizeVersion;
    LIST_ENTRY SrvOpenQLinks;
} MRX_SRV_OPEN, *PMRX_SRV_OPEN;

#define FOBX_FLAG_DFS_OPEN 0x0001
#define FOBX_FLAG_BAD_HANDLE 0x0002
#define FOBX_FLAG_BACKUP_INTENT 0x0004

typedef struct _MRX_PIPE_HANDLE_INFORMATION
{
    ULONG TypeOfPipe;
    ULONG ReadMode;
    ULONG CompletionMode;
} MRX_PIPE_HANDLE_INFORMATION, *PMRX_PIPE_HANDLE_INFORMATION;

typedef struct _MRX_FOBX_
{
    MRX_NORMAL_NODE_HEADER;
    PMRX_SRV_OPEN pSrvOpen;
    PFILE_OBJECT AssociatedFileObject;
    PVOID Context;
    PVOID Context2;
    ULONG Flags;
    union
    {
        struct
        {
            UNICODE_STRING UnicodeQueryTemplate;
        };
        PMRX_PIPE_HANDLE_INFORMATION PipeHandleInformation;
    };
    ULONG OffsetOfNextEaToReturn;
} MRX_FOBX, *PMRX_FOBX;

NTSTATUS
NTAPI
RxAcquireExclusiveFcbResourceInMRx(
    _Inout_ PMRX_FCB Fcb);

#ifndef DEFAULT_READ_AHEAD_GRANULARITY
#define DEFAULT_READ_AHEAD_GRANULARITY 0x08000
#endif
#ifndef FOBX_FLAG_DONT_ALLOW_FASTIO_READ
#define FOBX_FLAG_DONT_ALLOW_FASTIO_READ 0x0040
#endif
#ifndef FOBX_FLAG_DONT_ALLOW_PAGING_IO
#define FOBX_FLAG_DONT_ALLOW_PAGING_IO 0x0020
#endif
#ifndef FOBX_FLAG_FLUSH_EVEN_CACHED_READS
#define FOBX_FLAG_FLUSH_EVEN_CACHED_READS 0x0010
#endif
#ifndef FOBX_FLAG_NOT_USED
#define FOBX_FLAG_NOT_USED 0x0008
#endif
#ifndef MAX_PIPE_BUFFER_SIZE
#define MAX_PIPE_BUFFER_SIZE 0xFFFF
#endif
#ifndef NETROOT_FLAG_DEFER_READAHEAD
#define NETROOT_FLAG_DEFER_READAHEAD 0x0004
#endif
#ifndef NETROOT_FLAG_FINALIZE_INVOKED
#define NETROOT_FLAG_FINALIZE_INVOKED 0x0010
#endif
#ifndef NETROOT_FLAG_UNIQUE_FILE_NAME
#define NETROOT_FLAG_UNIQUE_FILE_NAME 0x0020
#endif
#ifndef NETROOT_FLAG_VOLUMEID_INITIALIZED
#define NETROOT_FLAG_VOLUMEID_INITIALIZED 0x0008
#endif
#ifndef SRVCALL_FLAG_FILE_SERVER
#define SRVCALL_FLAG_FILE_SERVER 0x2
#endif
#ifndef SRVCALL_FLAG_LOOPBACK_SERVER
#define SRVCALL_FLAG_LOOPBACK_SERVER 0x80
#endif
#ifndef SRVCALL_FLAG_LWIO_AWARE_SERVER
#define SRVCALL_FLAG_LWIO_AWARE_SERVER 0x40
#endif
#ifndef SRVCALL_FLAG_MAILSLOT_SERVER
#define SRVCALL_FLAG_MAILSLOT_SERVER 0x1
#endif
#ifndef VNETROOT_FLAG_FINALIZE_INVOKED
#define VNETROOT_FLAG_FINALIZE_INVOKED 0x00000002
#endif
#ifndef VNETROOT_FLAG_FORCED_FINALIZE
#define VNETROOT_FLAG_FORCED_FINALIZE 0x00000004
#endif
#ifndef VNETROOT_FLAG_NOT_FINALIZED
#define VNETROOT_FLAG_NOT_FINALIZED 0x00000008
#endif

#define MRX_NET_ROOT_STATE_CONNECTED ((UCHAR)4)
#define MRX_NET_ROOT_STATE_DISCONNECTED ((UCHAR)2)
#define MRX_NET_ROOT_STATE_ERROR ((UCHAR)3)
#define MRX_NET_ROOT_STATE_PAUSED ((UCHAR)1)
#define MRX_NET_ROOT_STATE_RECONN ((UCHAR)5)
#define MRX_PURGE_SAME_NETROOT ((UCHAR)0)
#define MRX_PURGE_SAME_SRVCALL ((UCHAR)1)
#define MRX_PURGE_SYNC_AT_NETROOT ((UCHAR)0)
#define MRX_PURGE_SYNC_AT_SRVCALL ((UCHAR)1)
#define NET_ROOT_COMM ((UCHAR)2)

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
NTSTATUS RxAcquireSharedFcbResourceInMRx(PMRX_FCB Fcb);
NTSTATUS RxAcquireSharedFcbResourceInMRxEx(PRX_CONTEXT pRxContext, PMRX_FCB Fcb);
void RxReleaseFcbResourceForThreadInMRx(PRX_CONTEXT pRxContext, PMRX_FCB MrxFcb, ERESOURCE_THREAD ResourceThreadId);
void RxReleaseFcbResourceInMRx(PMRX_FCB Fcb);

#ifdef __cplusplus
}
#endif

#define RxInitializeNetRootThrottlingParameters(__tp, __incr, __maxdelay) { PNETROOT_THROTTLING_PARAMETERS tp = (__tp); tp -> Increment = (__incr); tp -> MaximumDelay = (__maxdelay); }

#endif
