/*
 * Copyright (C) 2000 Francois Gouget
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

#ifndef __WINE_RPCDCEP_H
#define __WINE_RPCDCEP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _RPC_VERSION {
    unsigned short MajorVersion;
    unsigned short MinorVersion;
} RPC_VERSION;

typedef struct _RPC_SYNTAX_IDENTIFIER {
    GUID SyntaxGUID;
    RPC_VERSION SyntaxVersion;
} RPC_SYNTAX_IDENTIFIER, *PRPC_SYNTAX_IDENTIFIER;

typedef struct _RPC_MESSAGE
{
    RPC_BINDING_HANDLE Handle;
    ULONG DataRepresentation;
    void* Buffer;
    unsigned int BufferLength;
    unsigned int ProcNum;
    PRPC_SYNTAX_IDENTIFIER TransferSyntax;
    void* RpcInterfaceInformation;
    void* ReservedForRuntime;
    RPC_MGR_EPV* ManagerEpv;
    void* ImportContext;
    ULONG RpcFlags;
} RPC_MESSAGE, *PRPC_MESSAGE;

/* or'ed with ProcNum */
#define RPC_FLAGS_VALID_BIT         0x00008000

#define RPC_CONTEXT_HANDLE_DEFAULT_GUARD ((void *)0xfffff00d)

#define RPC_CONTEXT_HANDLE_DEFAULT_FLAGS    0x00000000
#define RPC_CONTEXT_HANDLE_FLAGS            0x30000000
#define RPC_CONTEXT_HANDLE_SERIALIZE        0x10000000
#define RPC_CONTEXT_HANDLE_DONT_SERIALIZE   0x20000000
#define RPC_TYPE_STRICT_CONTEXT_HANDLE      0x40000000

#define RPC_NCA_FLAGS_DEFAULT       0x00000000
#define RPC_NCA_FLAGS_IDEMPOTENT    0x00000001
#define RPC_NCA_FLAGS_BROADCAST     0x00000002
#define RPC_NCA_FLAGS_MAYBE         0x00000004

#define RPC_BUFFER_COMPLETE         0x00001000
#define RPC_BUFFER_PARTIAL          0x00002000
#define RPC_BUFFER_EXTRA            0x00004000
#define RPC_BUFFER_ASYNC            0x00008000
#define RPC_BUFFER_NONOTIFY         0x00010000

#define RPCFLG_MESSAGE              0x01000000
#define RPCFLG_HAS_MULTI_SYNTAXES   0x02000000
#define RPCFLG_HAS_CALLBACK         0x04000000
#define RPCFLG_AUTO_COMPLETE        0x08000000
#define RPCFLG_LOCAL_CALL           0x10000000
#define RPCFLG_INPUT_SYNCHRONOUS    0x20000000
#define RPCFLG_ASYNCHRONOUS         0x40000000
#define RPCFLG_NON_NDR              0x80000000

typedef void  (__RPC_STUB *RPC_DISPATCH_FUNCTION)(PRPC_MESSAGE Message);
typedef RPC_STATUS RPC_ENTRY RPC_FORWARD_FUNCTION(UUID *InterfaceId, RPC_VERSION *InterfaceVersion, UUID *ObjectId, unsigned char *Rpcpro, void **ppDestEndpoint);

typedef struct
{
    unsigned int DispatchTableCount;
    RPC_DISPATCH_FUNCTION* DispatchTable;
    LONG_PTR Reserved;
} RPC_DISPATCH_TABLE, *PRPC_DISPATCH_TABLE;

typedef struct _RPC_PROTSEQ_ENDPOINT
{
    unsigned char* RpcProtocolSequence;
    unsigned char* Endpoint;
} RPC_PROTSEQ_ENDPOINT, *PRPC_PROTSEQ_ENDPOINT;

#define NT351_INTERFACE_SIZE 0x40
#define RPC_INTERFACE_HAS_PIPES 0x0001

typedef struct _RPC_SERVER_INTERFACE
{
    unsigned int Length;
    RPC_SYNTAX_IDENTIFIER InterfaceId;
    RPC_SYNTAX_IDENTIFIER TransferSyntax;
    PRPC_DISPATCH_TABLE DispatchTable;
    unsigned int RpcProtseqEndpointCount;
    PRPC_PROTSEQ_ENDPOINT RpcProtseqEndpoint;
    RPC_MGR_EPV* DefaultManagerEpv;
    void const* InterpreterInfo;
    unsigned int Flags;
} RPC_SERVER_INTERFACE, *PRPC_SERVER_INTERFACE;

typedef struct _RPC_CLIENT_INTERFACE
{
    unsigned int Length;
    RPC_SYNTAX_IDENTIFIER InterfaceId;
    RPC_SYNTAX_IDENTIFIER TransferSyntax;
    PRPC_DISPATCH_TABLE DispatchTable;
    unsigned int RpcProtseqEndpointCount;
    PRPC_PROTSEQ_ENDPOINT RpcProtseqEndpoint;
    ULONG_PTR Reserved;
    void const* InterpreterInfo;
    unsigned int Flags;
} RPC_CLIENT_INTERFACE, *PRPC_CLIENT_INTERFACE;

#define RPC_C_OPT_COOKIE_AUTH 7

typedef struct _RPC_C_OPT_COOKIE_AUTH_DESCRIPTOR
{
    ULONG BufferSize;
    char *Buffer;
} RPC_C_OPT_COOKIE_AUTH_DESCRIPTOR;

#define TRANSPORT_TYPE_CN   0x01
#define TRANSPORT_TYPE_DG   0x02
#define TRANSPORT_TYPE_LPC  0x04
#define TRANSPORT_TYPE_WMSG 0x08

RPCRTAPI RPC_STATUS RPC_ENTRY
  I_RpcNegotiateTransferSyntax( RPC_MESSAGE* Message );
RPCRTAPI RPC_STATUS RPC_ENTRY
  I_RpcGetBuffer( RPC_MESSAGE* Message );
RPCRTAPI RPC_STATUS RPC_ENTRY
  I_RpcGetBufferWithObject( RPC_MESSAGE* Message, UUID* ObjectUuid );
RPCRTAPI RPC_STATUS RPC_ENTRY
  I_RpcSendReceive( RPC_MESSAGE* Message );
RPCRTAPI RPC_STATUS RPC_ENTRY
  I_RpcFreeBuffer( RPC_MESSAGE* Message );
RPCRTAPI RPC_STATUS RPC_ENTRY
  I_RpcSend( RPC_MESSAGE* Message );
RPCRTAPI RPC_STATUS RPC_ENTRY
  I_RpcReceive( RPC_MESSAGE* Message, unsigned int Size );

RPCRTAPI void* RPC_ENTRY
  I_RpcAllocate( unsigned int Size );
RPCRTAPI void RPC_ENTRY
  I_RpcFree( void* Object );

RPCRTAPI RPC_BINDING_HANDLE RPC_ENTRY
  I_RpcGetCurrentCallHandle( void );

/*
 * The platform SDK headers don't define these functions at all if WINNT is defined
 * The MSVC6 headers define two different sets of functions :
 *  If WINNT and MSWMSG are defined, the NT versions are defined
 *  If WINNT is not defined, the windows 9x versions are defined.
 * Note that the prototypes for I_RpcBindingSetAsync are different for each case.
 *
 * Wine defaults to the WinNT case and only defines these function is MSWMSG is
 *  defined. Defining the NT functions by default causes MIDL generated proxies
 *  to not compile.
 */

#if 1  /* WINNT */
#ifdef MSWMSG

RPCRTAPI RPC_STATUS RPC_ENTRY
  I_RpcServerStartListening( HWND hWnd );
RPCRTAPI RPC_STATUS RPC_ENTRY
  I_RpcServerStopListening( void );
/* WINNT */
RPCRTAPI RPC_STATUS RPC_ENTRY
  I_GetThreadWindowHandle( HWND* hWnd );
RPCRTAPI RPC_STATUS RPC_ENTRY
  I_RpcAsyncSendReceive( RPC_MESSAGE* Message, void* Context, HWND hWnd );

typedef RPC_STATUS (*RPC_BLOCKING_FN)(void* hWnd, void* Context, HANDLE hSyncEvent);

RPCRTAPI RPC_STATUS RPC_ENTRY
  I_RpcBindingSetAsync( RPC_BINDING_HANDLE Binding, RPC_BLOCKING_FN BlockingFn );

RPCRTAPI UINT RPC_ENTRY
  I_RpcWindowProc( void* hWnd, UINT Message, UINT wParam, ULONG lParam );

RPCRTAPI RPC_STATUS RPC_ENTRY
  I_RpcSetWMsgEndpoint( WCHAR* Endpoint );

#endif

#else

/* WIN9x */
RPCRTAPI RPC_STATUS RPC_ENTRY
  I_RpcServerStartListening( void* hWnd );

RPCRTAPI RPC_STATUS RPC_ENTRY
  I_RpcServerStopListening( void );

typedef RPC_STATUS (*RPC_BLOCKING_FN)(void* hWnd, void* Context, void* hSyncEvent);

RPCRTAPI RPC_STATUS RPC_ENTRY
  I_RpcBindingSetAsync( RPC_BINDING_HANDLE Binding, RPC_BLOCKING_FN BlockingFn, ULONG ServerTid );

RPCRTAPI RPC_STATUS RPC_ENTRY
  I_RpcSetThreadParams( int fClientFree, void* Context, void* hWndClient );

RPCRTAPI UINT RPC_ENTRY
  I_RpcWindowProc( void* hWnd, unsigned int Message, unsigned int wParam, ULONG lParam );

#endif

RPCRTAPI RPC_STATUS RPC_ENTRY
  I_RpcBindingInqLocalClientPID (RPC_BINDING_HANDLE Binding, ULONG *Pid );

RPCRTAPI RPC_STATUS RPC_ENTRY
  I_RpcBindingInqTransportType( RPC_BINDING_HANDLE Binding, unsigned int* Type );

RPCRTAPI LONG RPC_ENTRY I_RpcMapWin32Status(RPC_STATUS);

#ifdef __cplusplus
}
#endif

#ifndef NDR_CUSTOM_OR_DEFAULT_ALLOCATOR
#define NDR_CUSTOM_OR_DEFAULT_ALLOCATOR 0x10000000UL
#endif
#ifndef NDR_DEFAULT_ALLOCATOR
#define NDR_DEFAULT_ALLOCATOR 0x20000000UL
#endif
#ifndef RPCFLG_ACCESSIBILITY_BIT1
#define RPCFLG_ACCESSIBILITY_BIT1 0x00100000UL
#endif
#ifndef RPCFLG_ACCESSIBILITY_BIT2
#define RPCFLG_ACCESSIBILITY_BIT2 0x00200000UL
#endif
#ifndef RPCFLG_ACCESS_LOCAL
#define RPCFLG_ACCESS_LOCAL 0x00400000UL
#endif
#ifndef RPCFLG_HAS_GUARANTEE
#define RPCFLG_HAS_GUARANTEE 0x00000010UL
#endif
#ifndef RPCFLG_NDR64_CONTAINS_ARM_LAYOUT
#define RPCFLG_NDR64_CONTAINS_ARM_LAYOUT 0x04000000UL
#endif
#ifndef RPCFLG_SENDER_WAITING_FOR_REPLY
#define RPCFLG_SENDER_WAITING_FOR_REPLY 0x00800000UL
#endif
#ifndef RPCFLG_WINRT_REMOTE_ASYNC
#define RPCFLG_WINRT_REMOTE_ASYNC 0x00000020UL
#endif
#ifndef RPC_C_OPT_PRIVATE_DO_NOT_DISTURB
#define RPC_C_OPT_PRIVATE_DO_NOT_DISTURB 2
#endif
#ifndef RPC_C_OPT_PRIVATE_SUPPRESS_WAKE
#define RPC_C_OPT_PRIVATE_SUPPRESS_WAKE 1
#endif
#ifndef RPC_C_OPT_RESOURCE_TYPE_UUID
#define RPC_C_OPT_RESOURCE_TYPE_UUID 8
#endif
#ifndef RPC_C_OPT_SESSION_ID
#define RPC_C_OPT_SESSION_ID 6
#endif
#ifndef RPC_PROXY_CONNECTION_TYPE_IN_PROXY
#define RPC_PROXY_CONNECTION_TYPE_IN_PROXY 0
#endif
#ifndef RPC_PROXY_CONNECTION_TYPE_OUT_PROXY
#define RPC_PROXY_CONNECTION_TYPE_OUT_PROXY 1
#endif
#ifndef RPC_P_ADDR_FORMAT_TCP_IPV4
#define RPC_P_ADDR_FORMAT_TCP_IPV4 1
#endif
#ifndef RPC_P_ADDR_FORMAT_TCP_IPV6
#define RPC_P_ADDR_FORMAT_TCP_IPV6 2
#endif
#ifndef RPC_SYSTEM_HANDLE_FREE_ALL
#define RPC_SYSTEM_HANDLE_FREE_ALL 3
#endif
#ifndef RPC_SYSTEM_HANDLE_FREE_ERROR_ON_CLOSE
#define RPC_SYSTEM_HANDLE_FREE_ERROR_ON_CLOSE 4
#endif
#ifndef RPC_SYSTEM_HANDLE_FREE_RETRIEVED
#define RPC_SYSTEM_HANDLE_FREE_RETRIEVED 2
#endif
#ifndef RPC_SYSTEM_HANDLE_FREE_UNRETRIEVED
#define RPC_SYSTEM_HANDLE_FREE_UNRETRIEVED 1
#endif

typedef enum _LRPC_SYSTEM_HANDLE_MARSHAL_DIRECTION {
    MarshalDirectionMarshal = 0,
    MarshalDirectionUnmarshal = 1
} LRPC_SYSTEM_HANDLE_MARSHAL_DIRECTION;

enum RPC_ADDRESS_CHANGE_TYPE {
    PROTOCOL_NOT_LOADED = 1,
    PROTOCOL_LOADED = 2,
    PROTOCOL_ADDRESS_CHANGE = 3
};

typedef enum RpcProxyPerfCounters {
    RpcCurrentUniqueUser = 1,
    RpcBackEndConnectionAttempts = 2,
    RpcBackEndConnectionFailed = 3,
    RpcRequestsPerSecond = 4,
    RpcIncomingConnections = 5,
    RpcIncomingBandwidth = 6,
    RpcOutgoingBandwidth = 7,
    RpcAttemptedLbsDecisions = 8,
    RpcFailedLbsDecisions = 9,
    RpcAttemptedLbsMessages = 10,
    RpcFailedLbsMessages = 11,
    RpcLastCounter = 12
} RpcPerfCounters;

#ifdef __cplusplus
extern "C" {
#endif
RPC_STATUS WINAPI I_RpcOpenClientProcess(RPC_BINDING_HANDLE Binding, unsigned long DesiredAccess, void ** ClientProcess);
RPC_STATUS WINAPI I_UuidCreate(UUID * Uuid);
#ifdef __cplusplus
}
#endif

typedef struct _RPC_SEC_CONTEXT_KEY_INFO {
  unsigned long EncryptAlgorithm;
  unsigned long KeySize;
  unsigned long SignatureAlgorithm;
} RPC_SEC_CONTEXT_KEY_INFO, *PRPC_SEC_CONTEXT_KEY_INFO;

typedef struct _RPC_TRANSFER_SYNTAX {
  UUID Uuid;
  unsigned short VersMajor;
  unsigned short VersMinor;
} RPC_TRANSFER_SYNTAX;

#ifdef UNICODE
#define I_RpcBindingInqDynamicEndpoint I_RpcBindingInqDynamicEndpointW
#else
#ifdef UNICODE
#define I_RpcBindingInqDynamicEndpoint I_RpcBindingInqDynamicEndpointW
#else
#define I_RpcBindingInqDynamicEndpoint I_RpcBindingInqDynamicEndpointA
#endif
#endif
#define I_RpcNsBindingSetEntryName 0
#ifdef UNICODE
#define I_RpcServerUseProtseq2 I_RpcServerUseProtseq2W
#else
#ifdef UNICODE
#define I_RpcServerUseProtseq2 I_RpcServerUseProtseq2W
#else
#define I_RpcServerUseProtseq2 I_RpcServerUseProtseq2A
#endif
#endif
#ifdef UNICODE
#define I_RpcServerUseProtseqEp2 I_RpcServerUseProtseqEp2W
#else
#ifdef UNICODE
#define I_RpcServerUseProtseqEp2 I_RpcServerUseProtseqEp2W
#else
#define I_RpcServerUseProtseqEp2 I_RpcServerUseProtseqEp2A
#endif
#endif
#define I_RRPCUNINITIALIZENDROLE_EXPORT_NAME 0x3E8

typedef void *I_RPC_MUTEX;

typedef RPC_STATUS (WINAPI *I_RpcProxyFilterIfFn)(void *, UUID *, unsigned short, int *);

typedef RPC_STATUS (WINAPI *I_RpcProxyGetClientAddressFn)(void *, char *, unsigned long *);

typedef RPC_STATUS (WINAPI *I_RpcProxyGetClientSessionAndResourceUUID)(void *, int *, UUID *, int *, UUID *);

typedef RPC_STATUS (WINAPI *I_RpcProxyGetConnectionTimeoutFn)(unsigned long *);

typedef RPC_STATUS (WINAPI *I_RpcProxyIsValidMachineFn)(RPC_WSTR, RPC_WSTR, unsigned long);

typedef void (WINAPI *I_RpcProxyUpdatePerfCounterBackendServerFn)(unsigned short *, int);

typedef void (WINAPI *I_RpcProxyUpdatePerfCounterFn)(RpcPerfCounters, int, unsigned long);

typedef void (*PRPC_RUNDOWN)(void *);

typedef void WINAPI RPC_ADDRESS_CHANGE_FN(void *);

typedef void (*RPCLT_PDU_FILTER_FUNC)(void *, unsigned int, int);

typedef void (__cdecl *RPC_SETFILTER_FUNC)(RPCLT_PDU_FILTER_FUNC);

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
RPC_STATUS I_RpcBindingCopy(RPC_BINDING_HANDLE SourceBinding, RPC_BINDING_HANDLE *DestinationBinding);
RPC_STATUS I_RpcBindingCreateNP(RPC_WSTR ServerName, RPC_WSTR ServiceName, RPC_WSTR NetworkOptions, RPC_BINDING_HANDLE *Binding);
RPC_STATUS I_RpcBindingHandleToAsyncHandle(RPC_BINDING_HANDLE Binding, void **AsyncHandle);
RPC_STATUS I_RpcBindingInqConnId(RPC_BINDING_HANDLE Binding, void **ConnId, int *pfFirstCall);
RPC_STATUS I_RpcBindingInqDynamicEndpointA(RPC_BINDING_HANDLE Binding, RPC_CSTR *DynamicEndpoint);
RPC_STATUS I_RpcBindingInqDynamicEndpointW(RPC_BINDING_HANDLE Binding, RPC_WSTR *DynamicEndpoint);
RPC_STATUS I_RpcBindingInqMarshalledTargetInfo(RPC_BINDING_HANDLE Binding, unsigned long *MarshalledTargetInfoSize, RPC_CSTR *MarshalledTargetInfo);
RPC_STATUS I_RpcBindingInqSecurityContext(RPC_BINDING_HANDLE Binding, void **SecurityContextHandle);
RPC_STATUS I_RpcBindingInqSecurityContextKeyInfo(RPC_BINDING_HANDLE Binding, void *KeyInfo);
RPC_STATUS I_RpcBindingInqWireIdForSnego(RPC_BINDING_HANDLE Binding, unsigned char *WireId);
RPC_STATUS I_RpcBindingIsClientLocal(RPC_BINDING_HANDLE BindingHandle, unsigned int *ClientLocalFlag);
RPC_STATUS I_RpcBindingIsServerLocal(RPC_BINDING_HANDLE Binding, unsigned int *ServerLocalFlag);
RPC_STATUS WINAPI I_RpcBindingSetPrivateOption(RPC_BINDING_HANDLE hBinding, unsigned long option, ULONG_PTR optionValue);
RPC_STATUS I_RpcBindingToStaticStringBindingW(RPC_BINDING_HANDLE Binding, unsigned short **StringBinding);
void I_RpcClearMutex(I_RPC_MUTEX Mutex);
RPC_STATUS I_RpcConnectionInqSockBuffSize(unsigned long *RecvBuffSize, unsigned long *SendBuffSize);
RPC_STATUS I_RpcConnectionSetSockBuffSize(unsigned long RecvBuffSize, unsigned long SendBuffSize);
void I_RpcDeleteMutex(I_RPC_MUTEX Mutex);
RPC_STATUS I_RpcFreePipeBuffer(RPC_MESSAGE *Message);
void I_RpcFreeSystemHandle(unsigned char Type, void *Handle);
unsigned long I_RpcFreeSystemHandleCollection(void *CallObj, unsigned long FreeFlags);
RPC_STATUS I_RpcGetAssociationContext(RPC_BINDING_HANDLE BindingHandle, void **AssociationContext);
RPC_STATUS I_RpcGetDefaultSD(void **ppSecurityDescriptor);
RPC_STATUS I_RpcGetExtendedError(void);
void * I_RpcGetServerContextList(RPC_BINDING_HANDLE BindingHandle);
RPC_STATUS I_RpcGetSystemHandle(unsigned char *pMemory, unsigned char Type, unsigned long AccessMask, unsigned long HandleIndex, void *CallObj);
RPC_STATUS I_RpcIfInqTransferSyntaxes(RPC_IF_HANDLE RpcIfHandle, RPC_TRANSFER_SYNTAX *TransferSyntaxes, unsigned int TransferSyntaxSize, unsigned int *TransferSyntaxCount);
RPC_STATUS I_RpcLaunchDatagramReceiveThread(void *pAddress);
RPC_STATUS I_RpcMgmtEnableDedicatedThreadPool(void);
RPC_STATUS I_RpcMonitorAssociation(RPC_BINDING_HANDLE Handle, PRPC_RUNDOWN RundownRoutine, void *Context);
RPC_STATUS I_RpcNsBindingSetEntryNameA(RPC_BINDING_HANDLE Binding, unsigned long EntryNameSyntax, RPC_CSTR EntryName);
RPC_STATUS I_RpcNsBindingSetEntryNameW(RPC_BINDING_HANDLE Binding, unsigned long EntryNameSyntax, RPC_WSTR EntryName);
RPC_STATUS I_RpcNsInterfaceExported(unsigned long EntryNameSyntax, unsigned short *EntryName, RPC_SERVER_INTERFACE *RpcInterfaceInformation);
RPC_STATUS I_RpcNsInterfaceUnexported(unsigned long EntryNameSyntax, unsigned short *EntryName, RPC_SERVER_INTERFACE *RpcInterfaceInformation);
void I_RpcPauseExecution(unsigned long Milliseconds);
RPC_STATUS I_RpcReallocPipeBuffer(PRPC_MESSAGE Message, unsigned int NewSize);
RPC_STATUS I_RpcReplyToClientWithStatus(void *ConnectionParameter, RPC_STATUS RpcStatus);
void I_RpcRequestMutex(I_RPC_MUTEX *Mutex);
RPC_STATUS I_RpcServerCheckClientRestriction(RPC_BINDING_HANDLE Context);
long I_RpcServerDisableExceptionFilter(void);
RPC_STATUS WINAPI I_RpcServerGetAssociationID(RPC_BINDING_HANDLE Binding, unsigned long *AssociationID);
RPC_ADDRESS_CHANGE_FN * WINAPI I_RpcServerInqAddressChangeFn(void);
RPC_STATUS I_RpcServerInqLocalConnAddress(RPC_BINDING_HANDLE Binding, void *Buffer, unsigned long *BufferSize, unsigned long *AddressFormat);
RPC_STATUS I_RpcServerInqRemoteConnAddress(RPC_BINDING_HANDLE Binding, void *Buffer, unsigned long *BufferSize, unsigned long *AddressFormat);
RPC_STATUS I_RpcServerInqTransportType(unsigned int *Type);
RPC_STATUS I_RpcServerRegisterForwardFunction(RPC_FORWARD_FUNCTION *pForwardFunction);
RPC_STATUS WINAPI I_RpcServerSetAddressChangeFn(RPC_ADDRESS_CHANGE_FN *pAddressChangeFn);
RPC_STATUS I_RpcServerStartService(RPC_WSTR Protseq, RPC_WSTR Endpoint, RPC_IF_HANDLE IfSpec);
RPC_STATUS WINAPI I_RpcServerSubscribeForDisconnectNotification(RPC_BINDING_HANDLE Binding, void *hEvent);
RPC_STATUS I_RpcServerUseProtseq2A(RPC_CSTR NetworkAddress, RPC_CSTR Protseq, unsigned int MaxCalls, void *SecurityDescriptor, void *Policy);
RPC_STATUS I_RpcServerUseProtseq2W(RPC_WSTR NetworkAddress, RPC_WSTR Protseq, unsigned int MaxCalls, void *SecurityDescriptor, void *Policy);
RPC_STATUS I_RpcServerUseProtseqEp2A(RPC_CSTR NetworkAddress, RPC_CSTR Protseq, unsigned int MaxCalls, RPC_CSTR Endpoint, void *SecurityDescriptor, void *Policy);
RPC_STATUS I_RpcServerUseProtseqEp2W(RPC_WSTR NetworkAddress, RPC_WSTR Protseq, unsigned int MaxCalls, RPC_WSTR Endpoint, void *SecurityDescriptor, void *Policy);
void I_RpcSessionStrictContextHandle(void);
void I_RpcSetServerContextList(RPC_BINDING_HANDLE BindingHandle, void *ServerContextList);
RPC_STATUS I_RpcSetSystemHandle(void *Handle, unsigned char Type, unsigned long AccessMask, void *CallObj, unsigned long *HandleIndex);
void I_RpcSsDontSerializeContext(void);
RPC_STATUS I_RpcStopMonitorAssociation(RPC_BINDING_HANDLE Handle);
RPC_STATUS I_RpcSystemHandleTypeSpecificWork(void *Handle, unsigned char ActualType, unsigned char IdlType, LRPC_SYSTEM_HANDLE_MARSHAL_DIRECTION MarshalDirection);
RPC_STATUS I_RpcTurnOnEEInfoPropagation(void);
void I_RpcUninitializeNdrOle(void);

#ifdef __cplusplus
}
#endif

struct _RDR_CALLOUT_STATE
{
    RPC_STATUS LastError;
    void *LastEEInfo;
    RPC_HTTP_REDIRECTOR_STAGE LastCalledStage;
    unsigned short *ServerName;
    unsigned short *ServerPort;
    unsigned short *RemoteUser;
    unsigned short *AuthType;
    unsigned char ResourceTypePresent;
    unsigned char SessionIdPresent;
    unsigned char InterfacePresent;
    UUID ResourceType;
    UUID SessionId;
    RPC_SYNTAX_IDENTIFIER Interface;
    void *CertContext;
};

typedef struct tagI_RpcProxyCallbackInterface I_RpcProxyCallbackInterface;

typedef struct _RDR_CALLOUT_STATE RDR_CALLOUT_STATE;

typedef void (WINAPI *I_RpcFreeCalloutStateFn)(RDR_CALLOUT_STATE *);

typedef RPC_STATUS (WINAPI *I_RpcPerformCalloutFn)(void *, RDR_CALLOUT_STATE *, RPC_HTTP_REDIRECTOR_STAGE);

struct tagI_RpcProxyCallbackInterface
{
    I_RpcProxyIsValidMachineFn IsValidMachineFn;
    I_RpcProxyGetClientAddressFn GetClientAddressFn;
    I_RpcProxyGetConnectionTimeoutFn GetConnectionTimeoutFn;
    I_RpcPerformCalloutFn PerformCalloutFn;
    I_RpcFreeCalloutStateFn FreeCalloutStateFn;
    I_RpcProxyGetClientSessionAndResourceUUID GetClientSessionAndResourceUUIDFn;
    I_RpcProxyFilterIfFn ProxyFilterIfFn;
    I_RpcProxyUpdatePerfCounterFn RpcProxyUpdatePerfCounterFn;
    I_RpcProxyUpdatePerfCounterBackendServerFn RpcProxyUpdatePerfCounterBackendServerFn;
};

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
RPC_STATUS I_RpcProxyNewConnection(unsigned long ConnectionType, unsigned short *ServerAddress, unsigned short *ServerPort, unsigned short *MinConnTimeout, void *ConnectionParameter, RDR_CALLOUT_STATE *CallOutState, I_RpcProxyCallbackInterface *ProxyCallbackInterface);
void I_RpcRecordCalloutFailure(RPC_STATUS RpcStatus, RDR_CALLOUT_STATE *CallOutState, unsigned short *DllName);

#ifdef __cplusplus
}
#endif

#endif /*__WINE_RPCDCEP_H */
