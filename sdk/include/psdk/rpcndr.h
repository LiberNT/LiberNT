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

#ifndef __RPCNDR_H_VERSION__
#define __RPCNDR_H_VERSION__ ( 500 )

#endif

#ifndef __WINE_RPCNDR_H
#define __WINE_RPCNDR_H

#include <basetsd.h>
#include <rpcsal.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4201)
#pragma warning(disable:4255)
#pragma warning(disable:4820)
#endif

#undef CONST_VTBL
#ifdef CONST_VTABLE
# define CONST_VTBL const
#else
# define CONST_VTBL
#endif

#ifndef EXTERN_GUID
#ifdef __cplusplus
#define EXTERN_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
        EXTERN_C const GUID DECLSPEC_SELECTANY name = \
	{ l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }
#else
#define EXTERN_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
        EXTERN_C const GUID name
#endif
#endif

#define NDR_CHAR_REP_MASK             ((ULONG)0x000f)
#define NDR_INT_REP_MASK              ((ULONG)0x00f0)
#define NDR_FLOAT_REP_MASK            ((ULONG)0xff00)
#define NDR_IEEE_FLOAT                ((ULONG)0x0000)
#define NDR_VAX_FLOAT                 ((ULONG)0x0100)
#define NDR_IBM_FLOAT                 ((ULONG)0x0300)
#define NDR_ASCII_CHAR                ((ULONG)0x0000)
#define NDR_EBCDIC_CHAR               ((ULONG)0x0001)
#define NDR_LITTLE_ENDIAN             ((ULONG)0x0010)
#define NDR_BIG_ENDIAN                ((ULONG)0x0000)
#define NDR_LOCAL_DATA_REPRESENTATION NDR_LITTLE_ENDIAN
#define NDR_LOCAL_ENDIAN              NDR_LITTLE_ENDIAN

#define TARGET_IS_NT50_OR_LATER 1
#define TARGET_IS_NT40_OR_LATER 1
#define TARGET_IS_NT351_OR_WIN95_OR_LATER 1

#define small char
typedef unsigned char byte;
typedef INT64 hyper;
typedef UINT64 MIDL_uhyper;
typedef unsigned char boolean;

#ifndef _ERROR_STATUS_T_DEFINED
typedef ULONG error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif

#define __RPC_CALLEE WINAPI
#define RPC_VAR_ENTRY WINAPIV
#define NDR_SHAREABLE static

#define MIDL_ascii_strlen(s) strlen(s)
#define MIDL_ascii_strcpy(d,s) strcpy(d,s)
#define MIDL_memset(d,v,n) memset(d,v,n)
#define midl_user_free MIDL_user_free
#define midl_user_allocate MIDL_user_allocate

void __RPC_USER MIDL_user_free(void *);
void * __RPC_USER MIDL_user_allocate(SIZE_T) __WINE_ALLOC_SIZE(1) __WINE_DEALLOC(MIDL_user_free) __WINE_MALLOC;

#define NdrFcShort(s) (unsigned char)(s & 0xff), (unsigned char)(s >> 8)
#define NdrFcLong(s)  (unsigned char)(s & 0xff), (unsigned char)((s & 0x0000ff00) >> 8), \
  (unsigned char)((s & 0x00ff0000) >> 16), (unsigned char)(s >> 24)

#define RPC_BAD_STUB_DATA_EXCEPTION_FILTER  \
  ((RpcExceptionCode() == STATUS_ACCESS_VIOLATION) || \
   (RpcExceptionCode() == STATUS_DATATYPE_MISALIGNMENT) || \
   (RpcExceptionCode() == RPC_X_BAD_STUB_DATA) || \
   (RpcExceptionCode() == RPC_S_INVALID_BOUND))

typedef struct _NDR_SCONTEXT
{
  void *pad[2];
  void *userContext;
} *NDR_SCONTEXT;

#define NDRSContextValue(hContext) (&(hContext)->userContext)
#define cbNDRContext 20

typedef void (__RPC_USER *NDR_RUNDOWN)(void *context);
typedef void (__RPC_USER *NDR_NOTIFY_ROUTINE)(void);
typedef void (__RPC_USER *NDR_NOTIFY2_ROUTINE)(boolean flag);

#ifdef __REACTOS__
#ifndef __has_declspec_attribute
# if defined(_MSC_VER)
#  define __has_declspec_attribute(x) 1
# else
#  define __has_declspec_attribute(x) 0
# endif
#endif
#endif

#ifndef DECLSPEC_NOVTABLE
# if __has_declspec_attribute(novtable) && defined(__cplusplus)
#  define DECLSPEC_NOVTABLE __declspec(novtable)
# else
#  define DECLSPEC_NOVTABLE
# endif
#endif

#ifndef DECLSPEC_UUID
# if __has_declspec_attribute(uuid) && defined (__cplusplus)
#  define DECLSPEC_UUID(x) __declspec(uuid(x))
# else
#  define DECLSPEC_UUID(x)
# endif
#endif

#define MIDL_INTERFACE(x)   struct DECLSPEC_UUID(x) DECLSPEC_NOVTABLE

#if defined(__cplusplus) && !defined(_MSC_VER) && !defined(__CRT_UUID_DECL)
extern "C++" {
template <typename T> struct __reactos_uuidof_s;
template <typename T> struct __reactos_uuidof_s<T *> : __reactos_uuidof_s<T> {};
template <typename T> struct __reactos_uuidof_s<const T> : __reactos_uuidof_s<T> {};
}
#if __cpp_constexpr >= 200704L && __cpp_inline_variables >= 201606L
#define __CRT_UUID_DECL(type, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    extern "C++" { \
    template <> struct __reactos_uuidof_s<type> \
    { \
        static constexpr GUID value = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}; \
        static constexpr const GUID &get() { return value; } \
    }; \
    }
#else
#define __CRT_UUID_DECL(type, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    extern "C++" { \
    template <> struct __reactos_uuidof_s<type> \
    { \
        static const GUID &get() \
        { \
            static const GUID value = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}; \
            return value; \
        } \
    }; \
    }
#endif
#define __uuidof(type) (__reactos_uuidof_s<__typeof(type)>::get())
#endif

struct _MIDL_STUB_MESSAGE;
struct _MIDL_STUB_DESC;
struct _FULL_PTR_XLAT_TABLES;
struct NDR_ALLOC_ALL_NODES_CONTEXT;
struct NDR_POINTER_QUEUE_STATE;

typedef unsigned char *RPC_BUFPTR;
typedef ULONG RPC_LENGTH;
typedef void (__RPC_USER *EXPR_EVAL)(struct _MIDL_STUB_MESSAGE *);
typedef const unsigned char *PFORMAT_STRING;

typedef struct
{
  LONG Dimension;
  ULONG *BufferConformanceMark;
  ULONG *BufferVarianceMark;
  ULONG *MaxCountArray;
  ULONG *OffsetArray;
  ULONG *ActualCountArray;
} ARRAY_INFO, *PARRAY_INFO;

typedef struct
{
  ULONG WireCodeset;
  ULONG DesiredReceivingCodeset;
  void *CSArrayInfo;
} CS_STUB_INFO;

typedef struct _NDR_PIPE_DESC *PNDR_PIPE_DESC;
typedef struct _NDR_PIPE_MESSAGE *PNDR_PIPE_MESSAGE;
typedef struct _NDR_ASYNC_MESSAGE *PNDR_ASYNC_MESSAGE;
typedef struct _NDR_CORRELATION_INFO *PNDR_CORRELATION_INFO;

typedef struct _MIDL_STUB_MESSAGE
{
  PRPC_MESSAGE RpcMsg;
  unsigned char *Buffer;
  unsigned char *BufferStart;
  unsigned char *BufferEnd;
  unsigned char *BufferMark;
  ULONG BufferLength;
  ULONG MemorySize;
  unsigned char *Memory;
  unsigned char IsClient;
  unsigned char Pad;
  unsigned short uFlags2;
  int ReuseBuffer;
  struct NDR_ALLOC_ALL_NODES_CONTEXT *pAllocAllNodesContext;
  struct NDR_POINTER_QUEUE_STATE *pPointerQueueState;
  int IgnoreEmbeddedPointers;
  unsigned char *PointerBufferMark;
  unsigned char CorrDespIncrement;
  unsigned char uFlags;
  unsigned short UniquePtrCount;
  ULONG_PTR MaxCount;
  ULONG Offset;
  ULONG ActualCount;
  void * (__WINE_ALLOC_SIZE(1) __RPC_API *pfnAllocate)(SIZE_T);
  void (__RPC_API *pfnFree)(void *);
  unsigned char *StackTop;
  unsigned char *pPresentedType;
  unsigned char *pTransmitType;
  handle_t SavedHandle;
  const struct _MIDL_STUB_DESC *StubDesc;
  struct _FULL_PTR_XLAT_TABLES *FullPtrXlatTables;
  ULONG FullPtrRefId;
  ULONG PointerLength;
  unsigned int fInDontFree:1;
  unsigned int fDontCallFreeInst:1;
  unsigned int fUnused1 :1;
  unsigned int fHasReturn:1;
  unsigned int fHasExtensions:1;
  unsigned int fHasNewCorrDesc:1;
  unsigned int fIsIn:1;
  unsigned int fIsOut:1;
  unsigned int fIsOicf:1;
  unsigned int fBufferValid:1;
  unsigned int fHasMemoryValidateCallback:1;
  unsigned int fInFree:1;
  unsigned int fNeedMCCP:1;
  int fUnused2:3;
  int fUnused3:16;
  DWORD dwDestContext;
  void *pvDestContext;
  NDR_SCONTEXT *SavedContextHandles;
  LONG ParamNumber;
  struct IRpcChannelBuffer *pRpcChannelBuffer;
  PARRAY_INFO pArrayInfo;
  ULONG *SizePtrCountArray;
  ULONG *SizePtrOffsetArray;
  ULONG *SizePtrLengthArray;
  void *pArgQueue;
  DWORD dwStubPhase;
  void *LowStackMark;
  PNDR_ASYNC_MESSAGE pAsyncMsg;
  PNDR_CORRELATION_INFO pCorrInfo;
  unsigned char *pCorrMemory;
  void *pMemoryList;
  CS_STUB_INFO *pCSInfo;
  unsigned char *ConformanceMark;
  unsigned char *VarianceMark;
  INT_PTR Unused; /* BackingStoreLowMark on IA64 */
  struct _NDR_PROC_CONTEXT *pContext;
  void* ContextHandleHash;
  void* pUserMarshalList;
  INT_PTR Reserved51_3;
  INT_PTR Reserved51_4;
  INT_PTR Reserved51_5;
} MIDL_STUB_MESSAGE, *PMIDL_STUB_MESSAGE;

typedef void * (__RPC_API * GENERIC_BINDING_ROUTINE)(void *);
typedef void (__RPC_API * GENERIC_UNBIND_ROUTINE)(void *, unsigned char *);

typedef struct _GENERIC_BINDING_ROUTINE_PAIR
{
  GENERIC_BINDING_ROUTINE pfnBind;
  GENERIC_UNBIND_ROUTINE pfnUnbind;
} GENERIC_BINDING_ROUTINE_PAIR, *PGENERIC_BINDING_ROUTINE_PAIR;

typedef struct __GENERIC_BINDING_INFO
{
  void *pObj;
  unsigned int Size;
  GENERIC_BINDING_ROUTINE pfnBind;
  GENERIC_UNBIND_ROUTINE pfnUnbind;
} GENERIC_BINDING_INFO, *PGENERIC_BINDING_INFO;

typedef void (__RPC_USER *XMIT_HELPER_ROUTINE)(PMIDL_STUB_MESSAGE);

typedef struct _XMIT_ROUTINE_QUINTUPLE
{
  XMIT_HELPER_ROUTINE pfnTranslateToXmit;
  XMIT_HELPER_ROUTINE pfnTranslateFromXmit;
  XMIT_HELPER_ROUTINE pfnFreeXmit;
  XMIT_HELPER_ROUTINE pfnFreeInst;
} XMIT_ROUTINE_QUINTUPLE, *PXMIT_ROUTINE_QUINTUPLE;

typedef ULONG (__RPC_USER *USER_MARSHAL_SIZING_ROUTINE)(ULONG *, ULONG, void *);
typedef unsigned char * (__RPC_USER *USER_MARSHAL_MARSHALLING_ROUTINE)(ULONG *, unsigned char *, void *);
typedef unsigned char * (__RPC_USER *USER_MARSHAL_UNMARSHALLING_ROUTINE)(ULONG *, unsigned char *, void *);
typedef void (__RPC_USER *USER_MARSHAL_FREEING_ROUTINE)(ULONG *, void *);

typedef struct _USER_MARSHAL_ROUTINE_QUADRUPLE
{
  USER_MARSHAL_SIZING_ROUTINE pfnBufferSize;
  USER_MARSHAL_MARSHALLING_ROUTINE pfnMarshall;
  USER_MARSHAL_UNMARSHALLING_ROUTINE pfnUnmarshall;
  USER_MARSHAL_FREEING_ROUTINE pfnFree;
} USER_MARSHAL_ROUTINE_QUADRUPLE;

/* 'USRC' */
#define USER_MARSHAL_CB_SIGNATURE \
	( ( (DWORD)'U' << 24 ) | ( (DWORD)'S' << 16 ) | \
	  ( (DWORD)'R' << 8  ) | ( (DWORD)'C'       ) )

typedef enum
{
    USER_MARSHAL_CB_BUFFER_SIZE,
    USER_MARSHAL_CB_MARSHALL,
    USER_MARSHAL_CB_UNMARSHALL,
    USER_MARSHAL_CB_FREE
} USER_MARSHAL_CB_TYPE;

typedef struct _USER_MARSHAL_CB
{
    ULONG Flags;
    PMIDL_STUB_MESSAGE pStubMsg;
    PFORMAT_STRING pReserve;
    ULONG Signature;
    USER_MARSHAL_CB_TYPE CBType;
    PFORMAT_STRING pFormat;
    PFORMAT_STRING pTypeFormat;
} USER_MARSHAL_CB;

#define USER_CALL_CTXT_MASK(f) ((f) & 0x00ff)
#define USER_CALL_AUX_MASK(f) ((f) & 0xff00)
#define GET_USER_DATA_REP(f) HIWORD(f)

#define USER_CALL_IS_ASYNC 0x0100
#define USER_CALL_NEW_CORRELATION_DESC 0x0200

typedef struct _MALLOC_FREE_STRUCT
{
  void * (__WINE_ALLOC_SIZE(1) __RPC_USER *pfnAllocate)(SIZE_T);
  void   (__RPC_USER *pfnFree)(void *);
} MALLOC_FREE_STRUCT;

typedef struct _COMM_FAULT_OFFSETS
{
  short CommOffset;
  short FaultOffset;
} COMM_FAULT_OFFSETS;

typedef enum _IDL_CS_CONVERT
{
    IDL_CS_NO_CONVERT,
    IDL_CS_IN_PLACE_CONVERT,
    IDL_CS_NEW_BUFFER_CONVERT
} IDL_CS_CONVERT;

typedef void (__RPC_USER * CS_TYPE_NET_SIZE_ROUTINE)(RPC_BINDING_HANDLE,ULONG,ULONG,IDL_CS_CONVERT*,ULONG*,error_status_t*);
typedef void (__RPC_USER * CS_TYPE_TO_NETCS_ROUTINE)(RPC_BINDING_HANDLE,ULONG,void*,ULONG,byte*,ULONG*,error_status_t*);
typedef void (__RPC_USER * CS_TYPE_LOCAL_SIZE_ROUTINE)(RPC_BINDING_HANDLE,ULONG,ULONG,IDL_CS_CONVERT*,ULONG*,error_status_t*);
typedef void (__RPC_USER * CS_TYPE_FROM_NETCS_ROUTINE)(RPC_BINDING_HANDLE,ULONG,byte*,ULONG,ULONG,void*,ULONG*,error_status_t*);
typedef void (__RPC_USER * CS_TAG_GETTING_ROUTINE)(RPC_BINDING_HANDLE,int,ULONG*,ULONG*,ULONG*,error_status_t*);

typedef struct _NDR_CS_SIZE_CONVERT_ROUTINES
{
    CS_TYPE_NET_SIZE_ROUTINE   pfnNetSize;
    CS_TYPE_TO_NETCS_ROUTINE   pfnToNetCs;
    CS_TYPE_LOCAL_SIZE_ROUTINE pfnLocalSize;
    CS_TYPE_FROM_NETCS_ROUTINE pfnFromNetCs;
} NDR_CS_SIZE_CONVERT_ROUTINES;

typedef struct _NDR_CS_ROUTINES
{
    NDR_CS_SIZE_CONVERT_ROUTINES *pSizeConvertRoutines;
    CS_TAG_GETTING_ROUTINE       *pTagGettingRoutines;
} NDR_CS_ROUTINES;

typedef struct _NDR_EXPR_DESC
{
    const unsigned short *pOffset;
    PFORMAT_STRING        pFormatExpr;
} NDR_EXPR_DESC;

typedef struct _MIDL_STUB_DESC
{
  void *RpcInterfaceInformation;
  void * (__WINE_ALLOC_SIZE(1) __RPC_API *pfnAllocate)(SIZE_T);
  void (__RPC_API *pfnFree)(void *);
  union {
    handle_t *pAutoHandle;
    handle_t *pPrimitiveHandle;
    PGENERIC_BINDING_INFO pGenericBindingInfo;
  } IMPLICIT_HANDLE_INFO;
  const NDR_RUNDOWN *apfnNdrRundownRoutines;
  const GENERIC_BINDING_ROUTINE_PAIR *aGenericBindingRoutinePairs;
  const EXPR_EVAL *apfnExprEval;
  const XMIT_ROUTINE_QUINTUPLE *aXmitQuintuple;
  const unsigned char *pFormatTypes;
  int fCheckBounds;
  ULONG Version;
  MALLOC_FREE_STRUCT *pMallocFreeStruct;
  LONG MIDLVersion;
  const COMM_FAULT_OFFSETS *CommFaultOffsets;
  const USER_MARSHAL_ROUTINE_QUADRUPLE *aUserMarshalQuadruple;
  const NDR_NOTIFY_ROUTINE *NotifyRoutineTable;
  ULONG_PTR mFlags;
  const NDR_CS_ROUTINES *CsRoutineTables;
  void *ProxyServerInfo;
  const NDR_EXPR_DESC *pExprInfo;
} MIDL_STUB_DESC;
typedef const MIDL_STUB_DESC *PMIDL_STUB_DESC;

typedef struct _MIDL_FORMAT_STRING
{
  short Pad;
  unsigned char Format[];
} MIDL_FORMAT_STRING;

typedef struct _MIDL_METHOD_PROPERTY
{
    ULONG     Id;
    ULONG_PTR Value;
} MIDL_METHOD_PROPERTY, *PMIDL_METHOD_PROPERTY;

typedef struct _MIDL_METHOD_PROPERTY_MAP
{
    ULONG                       Count;
    const MIDL_METHOD_PROPERTY *Properties;
} MIDL_METHOD_PROPERTY_MAP, *PMIDL_METHOD_PROPERTY_MAP;

typedef struct _MIDL_INTERFACE_METHOD_PROPERTIES
{
    unsigned short MethodCount;
    const MIDL_METHOD_PROPERTY_MAP * const *MethodProperties;
} MIDL_INTERFACE_METHOD_PROPERTIES;

typedef struct _MIDL_SYNTAX_INFO
{
  RPC_SYNTAX_IDENTIFIER TransferSyntax;
  RPC_DISPATCH_TABLE* DispatchTable;
  PFORMAT_STRING ProcString;
  const unsigned short* FmtStringOffset;
  PFORMAT_STRING TypeString;
  const void* aUserMarshalQuadruple;
  const MIDL_INTERFACE_METHOD_PROPERTIES *pMethodProperties;
  ULONG_PTR pReserved2;
} MIDL_SYNTAX_INFO, *PMIDL_SYNTAX_INFO;

typedef void (__RPC_API *STUB_THUNK)( PMIDL_STUB_MESSAGE );

#ifndef WINE_NO_STRICT_PROTOTYPES
typedef LONG (__RPC_API *SERVER_ROUTINE)(void);
#else
typedef LONG (__RPC_API *SERVER_ROUTINE)();
#endif

typedef struct _MIDL_SERVER_INFO_
{
  PMIDL_STUB_DESC pStubDesc;
  const SERVER_ROUTINE *DispatchTable;
  PFORMAT_STRING ProcString;
  const unsigned short *FmtStringOffset;
  const STUB_THUNK *ThunkTable;
  PRPC_SYNTAX_IDENTIFIER pTransferSyntax;
  ULONG_PTR nCount;
  PMIDL_SYNTAX_INFO pSyntaxInfo;
} MIDL_SERVER_INFO, *PMIDL_SERVER_INFO;

typedef struct _MIDL_STUBLESS_PROXY_INFO
{
  PMIDL_STUB_DESC pStubDesc;
  PFORMAT_STRING ProcFormatString;
  const unsigned short *FormatStringOffset;
  PRPC_SYNTAX_IDENTIFIER pTransferSyntax;
  ULONG_PTR nCount;
  PMIDL_SYNTAX_INFO pSyntaxInfo;
} MIDL_STUBLESS_PROXY_INFO, *PMIDL_STUBLESS_PROXY_INFO;


#if defined(__i386__) && !defined(_MSC_VER) && !defined(__MINGW32__) && !defined(__CYGWIN__)
/* Calling convention for returning structures/unions is different between Windows and gcc on i386 */
typedef LONG_PTR CLIENT_CALL_RETURN;
#else
typedef union _CLIENT_CALL_RETURN
{
  void *Pointer;
  LONG_PTR Simple;
} CLIENT_CALL_RETURN;
#endif

typedef enum {
  STUB_UNMARSHAL,
  STUB_CALL_SERVER,
  STUB_MARSHAL,
  STUB_CALL_SERVER_NO_HRESULT
} STUB_PHASE;

typedef enum {
  PROXY_CALCSIZE,
  PROXY_GETBUFFER,
  PROXY_MARSHAL,
  PROXY_SENDRECEIVE,
  PROXY_UNMARSHAL
} PROXY_PHASE;

typedef enum {
  XLAT_SERVER = 1,
  XLAT_CLIENT
} XLAT_SIDE;

typedef struct _FULL_PTR_TO_REFID_ELEMENT {
  struct _FULL_PTR_TO_REFID_ELEMENT *Next;
  void *Pointer;
  ULONG RefId;
  unsigned char State;
} FULL_PTR_TO_REFID_ELEMENT, *PFULL_PTR_TO_REFID_ELEMENT;

/* Full pointer translation tables */
typedef struct _FULL_PTR_XLAT_TABLES {
  struct {
    void **XlatTable;
    unsigned char *StateTable;
    ULONG NumberOfEntries;
  } RefIdToPointer;

  struct {
    PFULL_PTR_TO_REFID_ELEMENT *XlatTable;
    ULONG NumberOfBuckets;
    ULONG HashMask;
  } PointerToRefId;

  ULONG                   NextRefId;
  XLAT_SIDE               XlatSide;
} FULL_PTR_XLAT_TABLES,  *PFULL_PTR_XLAT_TABLES;

struct IRpcStubBuffer;

typedef void  * NDR_CCONTEXT;

typedef struct _SCONTEXT_QUEUE {
  ULONG NumberOfObjects;
  NDR_SCONTEXT *ArrayOfObjects;
} SCONTEXT_QUEUE, *PSCONTEXT_QUEUE;

typedef struct _NDR_USER_MARSHAL_INFO_LEVEL1
{
    void *Buffer;
    ULONG BufferSize;
    void * (__WINE_ALLOC_SIZE(1) __RPC_API *pfnAllocate)(SIZE_T);
    void (__RPC_API *pfnFree)(void *);
    struct IRpcChannelBuffer *pRpcChannelBuffer;
    ULONG_PTR Reserved[5];
} NDR_USER_MARSHAL_INFO_LEVEL1;

typedef struct _NDR_USER_MARSHAL_INFO
{
    ULONG InformationLevel;
    union
    {
        NDR_USER_MARSHAL_INFO_LEVEL1 Level1;
    } DUMMYUNIONNAME1;
} NDR_USER_MARSHAL_INFO;

/* Context Handles */

RPCRTAPI RPC_BINDING_HANDLE RPC_ENTRY
  NDRCContextBinding( NDR_CCONTEXT CContext );

RPCRTAPI void RPC_ENTRY
  NDRCContextMarshall( NDR_CCONTEXT CContext, void *pBuff );

RPCRTAPI void RPC_ENTRY
  NDRCContextUnmarshall( NDR_CCONTEXT *pCContext, RPC_BINDING_HANDLE hBinding,
                         void *pBuff, ULONG DataRepresentation );

RPCRTAPI void RPC_ENTRY
  NDRSContextMarshall( NDR_SCONTEXT CContext, void *pBuff, NDR_RUNDOWN userRunDownIn );

RPCRTAPI NDR_SCONTEXT RPC_ENTRY
  NDRSContextUnmarshall( void *pBuff, ULONG DataRepresentation );

RPCRTAPI void RPC_ENTRY
  NDRSContextMarshallEx( RPC_BINDING_HANDLE BindingHandle, NDR_SCONTEXT CContext,
                         void *pBuff, NDR_RUNDOWN userRunDownIn );

RPCRTAPI void RPC_ENTRY
  NDRSContextMarshall2( RPC_BINDING_HANDLE BindingHandle, NDR_SCONTEXT CContext,
                        void *pBuff, NDR_RUNDOWN userRunDownIn, void * CtxGuard,
                        ULONG Flags );

RPCRTAPI NDR_SCONTEXT RPC_ENTRY
  NDRSContextUnmarshallEx( RPC_BINDING_HANDLE BindingHandle, void *pBuff,
                           ULONG DataRepresentation );

RPCRTAPI NDR_SCONTEXT RPC_ENTRY
  NDRSContextUnmarshall2( RPC_BINDING_HANDLE BindingHandle, void *pBuff,
                          ULONG DataRepresentation, void *CtxGuard,
                          ULONG Flags );

RPCRTAPI void RPC_ENTRY
  NdrClientContextMarshall ( PMIDL_STUB_MESSAGE pStubMsg, NDR_CCONTEXT ContextHandle, int fCheck );

RPCRTAPI void RPC_ENTRY
  NdrClientContextUnmarshall( PMIDL_STUB_MESSAGE pStubMsg, NDR_CCONTEXT* pContextHandle,
                              RPC_BINDING_HANDLE BindHandle );

RPCRTAPI void RPC_ENTRY
  NdrServerContextMarshall ( PMIDL_STUB_MESSAGE pStubMsg, NDR_SCONTEXT ContextHandle, NDR_RUNDOWN RundownRoutine );

RPCRTAPI NDR_SCONTEXT RPC_ENTRY
  NdrServerContextUnmarshall( PMIDL_STUB_MESSAGE pStubMsg );

RPCRTAPI void RPC_ENTRY
  NdrContextHandleSize( PMIDL_STUB_MESSAGE pStubMsg, unsigned char* pMemory, PFORMAT_STRING pFormat );

RPCRTAPI NDR_SCONTEXT RPC_ENTRY
  NdrContextHandleInitialize( PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat );

RPCRTAPI void RPC_ENTRY
  NdrServerContextNewMarshall( PMIDL_STUB_MESSAGE pStubMsg, NDR_SCONTEXT ContextHandle,
                               NDR_RUNDOWN RundownRoutine, PFORMAT_STRING pFormat );

RPCRTAPI NDR_SCONTEXT RPC_ENTRY
  NdrServerContextNewUnmarshall( PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat );

RPCRTAPI RPC_STATUS RPC_ENTRY
  RpcSmDestroyClientContext( void **ContextHandle );

RPCRTAPI void RPC_ENTRY
  RpcSsDestroyClientContext( void **ContextHandle );

RPCRTAPI void RPC_ENTRY
  NdrSimpleTypeMarshall( PMIDL_STUB_MESSAGE pStubMsg, unsigned char* pMemory, unsigned char FormatChar );
RPCRTAPI void RPC_ENTRY
  NdrSimpleTypeUnmarshall( PMIDL_STUB_MESSAGE pStubMsg, unsigned char* pMemory, unsigned char FormatChar );

RPCRTAPI unsigned char* RPC_ENTRY
  NdrByteCountPointerMarshall( PMIDL_STUB_MESSAGE pStubMsg, unsigned char* pMemory, PFORMAT_STRING pFormat );
RPCRTAPI unsigned char* RPC_ENTRY
  NdrByteCountPointerUnmarshall( PMIDL_STUB_MESSAGE pStubMsg, unsigned char** ppMemory, PFORMAT_STRING pFormat, unsigned char fMustAlloc );
RPCRTAPI void RPC_ENTRY
  NdrByteCountPointerBufferSize( PMIDL_STUB_MESSAGE pStubMsg, unsigned char* pMemory, PFORMAT_STRING pFormat );
RPCRTAPI void RPC_ENTRY
  NdrByteCountPointerFree( PMIDL_STUB_MESSAGE pStubMsg, unsigned char* pMemory, PFORMAT_STRING pFormat );

RPCRTAPI unsigned char* RPC_ENTRY
  NdrRangeUnmarshall( PMIDL_STUB_MESSAGE pStubMsg, unsigned char** ppMemory, PFORMAT_STRING pFormat, unsigned char fMustAlloc );

/* while MS declares each prototype separately, I prefer to use macros for this kind of thing instead */
#define SIMPLE_TYPE_MARSHAL(type) \
RPCRTAPI unsigned char* RPC_ENTRY \
  Ndr##type##Marshall( PMIDL_STUB_MESSAGE pStubMsg, unsigned char* pMemory, PFORMAT_STRING pFormat ); \
RPCRTAPI unsigned char* RPC_ENTRY \
  Ndr##type##Unmarshall( PMIDL_STUB_MESSAGE pStubMsg, unsigned char** ppMemory, PFORMAT_STRING pFormat, unsigned char fMustAlloc ); \
RPCRTAPI void RPC_ENTRY \
  Ndr##type##BufferSize( PMIDL_STUB_MESSAGE pStubMsg, unsigned char* pMemory, PFORMAT_STRING pFormat ); \
RPCRTAPI ULONG RPC_ENTRY \
  Ndr##type##MemorySize( PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat );

#define TYPE_MARSHAL(type) \
  SIMPLE_TYPE_MARSHAL(type) \
RPCRTAPI void RPC_ENTRY \
  Ndr##type##Free( PMIDL_STUB_MESSAGE pStubMsg, unsigned char* pMemory, PFORMAT_STRING pFormat );

TYPE_MARSHAL(Pointer)
TYPE_MARSHAL(SimpleStruct)
TYPE_MARSHAL(ConformantStruct)
TYPE_MARSHAL(ConformantVaryingStruct)
TYPE_MARSHAL(ComplexStruct)
TYPE_MARSHAL(FixedArray)
TYPE_MARSHAL(ConformantArray)
TYPE_MARSHAL(ConformantVaryingArray)
TYPE_MARSHAL(VaryingArray)
TYPE_MARSHAL(ComplexArray)
TYPE_MARSHAL(EncapsulatedUnion)
TYPE_MARSHAL(NonEncapsulatedUnion)
TYPE_MARSHAL(XmitOrRepAs)
TYPE_MARSHAL(UserMarshal)
TYPE_MARSHAL(InterfacePointer)

SIMPLE_TYPE_MARSHAL(ConformantString)
SIMPLE_TYPE_MARSHAL(NonConformantString)

#undef TYPE_MARSHAL
#undef SIMPLE_TYPE_MARSHAL

RPCRTAPI void RPC_ENTRY
  NdrCorrelationInitialize( PMIDL_STUB_MESSAGE pStubMsg, void *pMemory, ULONG CacheSize, ULONG flags );
RPCRTAPI void RPC_ENTRY
  NdrCorrelationPass( PMIDL_STUB_MESSAGE pStubMsg );
RPCRTAPI void RPC_ENTRY
  NdrCorrelationFree( PMIDL_STUB_MESSAGE pStubMsg );

RPCRTAPI void RPC_ENTRY
  NdrConvert2( PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat, LONG NumberParams );
RPCRTAPI void RPC_ENTRY
  NdrConvert( PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat );

#define USER_MARSHAL_FC_BYTE    1
#define USER_MARSHAL_FC_CHAR    2
#define USER_MARSHAL_FC_SMALL   3
#define USER_MARSHAL_FC_USMALL  4
#define USER_MARSHAL_FC_WCHAR   5
#define USER_MARSHAL_FC_SHORT   6
#define USER_MARSHAL_FC_USHORT  7
#define USER_MARSHAL_FC_LONG    8
#define USER_MARSHAL_FC_ULONG   9
#define USER_MARSHAL_FC_FLOAT   10
#define USER_MARSHAL_FC_HYPER   11
#define USER_MARSHAL_FC_DOUBLE  12

RPCRTAPI unsigned char* RPC_ENTRY
  NdrUserMarshalSimpleTypeConvert( ULONG *pFlags, unsigned char *pBuffer, unsigned char FormatChar );

CLIENT_CALL_RETURN RPC_VAR_ENTRY
  NdrClientCall( PMIDL_STUB_DESC pStubDescriptor, PFORMAT_STRING pFormat, ... );
CLIENT_CALL_RETURN RPC_VAR_ENTRY
  NdrClientCall2( PMIDL_STUB_DESC pStubDescriptor, PFORMAT_STRING pFormat, ... );
CLIENT_CALL_RETURN RPC_VAR_ENTRY
  NdrClientCall3( MIDL_STUBLESS_PROXY_INFO *info, ULONG proc, void *retval, ... );
CLIENT_CALL_RETURN RPC_VAR_ENTRY
  NdrAsyncClientCall( PMIDL_STUB_DESC pStubDescriptor, PFORMAT_STRING pFormat, ... );
CLIENT_CALL_RETURN RPC_VAR_ENTRY
  Ndr64AsyncClientCall( MIDL_STUBLESS_PROXY_INFO *info, ULONG proc, void *retval, ... );
CLIENT_CALL_RETURN RPC_VAR_ENTRY
  NdrDcomAsyncClientCall( PMIDL_STUB_DESC pStubDescriptor, PFORMAT_STRING pFormat, ... );

RPCRTAPI void RPC_ENTRY
  NdrServerCall2( PRPC_MESSAGE pRpcMsg );
RPCRTAPI void RPC_ENTRY
  NdrServerCall( PRPC_MESSAGE pRpcMsg );
RPCRTAPI void RPC_ENTRY
  NdrServerCallAll( PRPC_MESSAGE pRpcMsg );
RPCRTAPI void RPC_ENTRY
  NdrAsyncServerCall( PRPC_MESSAGE pRpcMsg );

RPCRTAPI LONG RPC_ENTRY
  NdrStubCall2( struct IRpcStubBuffer* pThis, struct IRpcChannelBuffer* pChannel, PRPC_MESSAGE pRpcMsg, DWORD * pdwStubPhase );
RPCRTAPI LONG RPC_ENTRY
  NdrStubCall( struct IRpcStubBuffer* pThis, struct IRpcChannelBuffer* pChannel, PRPC_MESSAGE pRpcMsg, DWORD * pdwStubPhase );
RPCRTAPI LONG RPC_ENTRY
  NdrAsyncStubCall( struct IRpcStubBuffer* pThis, struct IRpcChannelBuffer* pChannel, PRPC_MESSAGE pRpcMsg, DWORD * pdwStubPhase );
RPCRTAPI LONG RPC_ENTRY
  NdrDcomAsyncStubCall( struct IRpcStubBuffer* pThis, struct IRpcChannelBuffer* pChannel, PRPC_MESSAGE pRpcMsg, DWORD * pdwStubPhase );

RPCRTAPI void* RPC_ENTRY
  NdrAllocate( PMIDL_STUB_MESSAGE pStubMsg, SIZE_T Len ) __WINE_ALLOC_SIZE(2) __WINE_MALLOC;

RPCRTAPI void RPC_ENTRY
  NdrClearOutParameters( PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat, void *ArgAddr );

RPCRTAPI RPC_STATUS RPC_ENTRY
  NdrMapCommAndFaultStatus( PMIDL_STUB_MESSAGE pStubMsg, ULONG *pCommStatus,
                            ULONG *pFaultStatus, RPC_STATUS Status_ );

RPCRTAPI void RPC_ENTRY
  NdrOleFree( void* NodeToFree );
RPCRTAPI void* RPC_ENTRY
  NdrOleAllocate( SIZE_T Size ) __WINE_ALLOC_SIZE(1) __WINE_DEALLOC(NdrOleFree) __WINE_MALLOC;

RPCRTAPI void RPC_ENTRY
  NdrClientInitialize( PRPC_MESSAGE pRpcMessage, PMIDL_STUB_MESSAGE pStubMsg,
                       PMIDL_STUB_DESC pStubDesc, unsigned int ProcNum );
RPCRTAPI void RPC_ENTRY
  NdrClientInitializeNew( PRPC_MESSAGE pRpcMessage, PMIDL_STUB_MESSAGE pStubMsg,
                          PMIDL_STUB_DESC pStubDesc, unsigned int ProcNum );
RPCRTAPI unsigned char* RPC_ENTRY
  NdrServerInitialize( PRPC_MESSAGE pRpcMsg, PMIDL_STUB_MESSAGE pStubMsg, PMIDL_STUB_DESC pStubDesc );
RPCRTAPI unsigned char* RPC_ENTRY
  NdrServerInitializeNew( PRPC_MESSAGE pRpcMsg, PMIDL_STUB_MESSAGE pStubMsg, PMIDL_STUB_DESC pStubDesc );
RPCRTAPI unsigned char* RPC_ENTRY
  NdrServerInitializeUnmarshall( PMIDL_STUB_MESSAGE pStubMsg, PMIDL_STUB_DESC pStubDesc, PRPC_MESSAGE pRpcMsg );
RPCRTAPI void RPC_ENTRY
  NdrServerInitializeMarshall( PRPC_MESSAGE pRpcMsg, PMIDL_STUB_MESSAGE pStubMsg  );
RPCRTAPI void RPC_ENTRY
  NdrServerMarshall( struct IRpcStubBuffer *pThis, struct IRpcChannelBuffer *pChannel, PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat );
RPCRTAPI void RPC_ENTRY
  NdrServerUnmarshall( struct IRpcChannelBuffer *pChannel, PRPC_MESSAGE pRpcMsg,
                       PMIDL_STUB_MESSAGE pStubMsg, PMIDL_STUB_DESC pStubDesc,
                       PFORMAT_STRING pFormat, void *pParamList );
RPCRTAPI unsigned char* RPC_ENTRY
  NdrGetBuffer( PMIDL_STUB_MESSAGE stubmsg, ULONG buflen, RPC_BINDING_HANDLE handle )
  __WINE_ALLOC_SIZE(2) __WINE_MALLOC;
RPCRTAPI void RPC_ENTRY
  NdrFreeBuffer( PMIDL_STUB_MESSAGE pStubMsg );
RPCRTAPI unsigned char* RPC_ENTRY
  NdrSendReceive( PMIDL_STUB_MESSAGE stubmsg, unsigned char *buffer );

RPCRTAPI unsigned char * RPC_ENTRY
  NdrNsGetBuffer( PMIDL_STUB_MESSAGE pStubMsg, ULONG BufferLength, RPC_BINDING_HANDLE Handle );
RPCRTAPI unsigned char * RPC_ENTRY
  NdrNsSendReceive( PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pBufferEnd, RPC_BINDING_HANDLE *pAutoHandle );

RPCRTAPI RPC_STATUS RPC_ENTRY
  NdrGetDcomProtocolVersion( PMIDL_STUB_MESSAGE pStubMsg, RPC_VERSION *pVersion );

RPCRTAPI PFULL_PTR_XLAT_TABLES RPC_ENTRY
  NdrFullPointerXlatInit( ULONG NumberOfPointers, XLAT_SIDE XlatSide );
RPCRTAPI void RPC_ENTRY
  NdrFullPointerXlatFree( PFULL_PTR_XLAT_TABLES pXlatTables );
RPCRTAPI int RPC_ENTRY
  NdrFullPointerQueryPointer( PFULL_PTR_XLAT_TABLES pXlatTables, void *pPointer,
                              unsigned char QueryType, ULONG *pRefId );
RPCRTAPI int RPC_ENTRY
  NdrFullPointerQueryRefId( PFULL_PTR_XLAT_TABLES pXlatTables, ULONG RefId,
                            unsigned char QueryType, void **ppPointer );
RPCRTAPI void RPC_ENTRY
  NdrFullPointerInsertRefId( PFULL_PTR_XLAT_TABLES pXlatTables, ULONG RefId, void *pPointer );
RPCRTAPI int RPC_ENTRY
  NdrFullPointerFree( PFULL_PTR_XLAT_TABLES pXlatTables, void *Pointer );

RPCRTAPI void RPC_ENTRY
  NdrRpcSsEnableAllocate( PMIDL_STUB_MESSAGE pMessage );
RPCRTAPI void RPC_ENTRY
  NdrRpcSsDisableAllocate( PMIDL_STUB_MESSAGE pMessage );
RPCRTAPI void RPC_ENTRY
  NdrRpcSmSetClientToOsf( PMIDL_STUB_MESSAGE pMessage );
RPCRTAPI void RPC_ENTRY
  NdrRpcSmClientFree( void *NodeToFree );
RPCRTAPI void * RPC_ENTRY
  NdrRpcSmClientAllocate( SIZE_T Size ) __WINE_ALLOC_SIZE(1) __WINE_DEALLOC(NdrRpcSmClientFree) __WINE_MALLOC;
RPCRTAPI void RPC_ENTRY
  NdrRpcSsDefaultFree( void *NodeToFree );
RPCRTAPI void * RPC_ENTRY
  NdrRpcSsDefaultAllocate( SIZE_T Size ) __WINE_ALLOC_SIZE(1) __WINE_DEALLOC(NdrRpcSsDefaultFree) __WINE_MALLOC;

RPCRTAPI RPC_STATUS RPC_ENTRY
  NdrGetUserMarshalInfo( ULONG *pFlags, ULONG InformationLevel, NDR_USER_MARSHAL_INFO *pMarshalInfo );

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#ifdef __cplusplus
}
#endif
#ifndef TARGET_IS_NT100_OR_LATER
#define TARGET_IS_NT100_OR_LATER 1
#endif
#ifndef TARGET_IS_NT51_OR_LATER
#define TARGET_IS_NT51_OR_LATER 1
#endif
#ifndef TARGET_IS_NT60_OR_LATER
#define TARGET_IS_NT60_OR_LATER 1
#endif
#ifndef TARGET_IS_NT61_OR_LATER
#define TARGET_IS_NT61_OR_LATER 1
#endif
#ifndef TARGET_IS_NT62_OR_LATER
#define TARGET_IS_NT62_OR_LATER 1
#endif
#ifndef TARGET_IS_NT63_OR_LATER
#define TARGET_IS_NT63_OR_LATER 1
#endif

typedef enum _system_handle_t {
    SYSTEM_HANDLE_FILE = 0,
    SYSTEM_HANDLE_SEMAPHORE = 1,
    SYSTEM_HANDLE_EVENT = 2,
    SYSTEM_HANDLE_MUTEX = 3,
    SYSTEM_HANDLE_PROCESS = 4,
    SYSTEM_HANDLE_TOKEN = 5,
    SYSTEM_HANDLE_SECTION = 6,
    SYSTEM_HANDLE_REG_KEY = 7,
    SYSTEM_HANDLE_THREAD = 8,
    SYSTEM_HANDLE_COMPOSITION_OBJECT = 9,
    SYSTEM_HANDLE_SOCKET = 10,
    SYSTEM_HANDLE_JOB = 11,
    SYSTEM_HANDLE_PIPE = 12,
    SYSTEM_HANDLE_MAX = 12,
    SYSTEM_HANDLE_INVALID = 255
} system_handle_t;

enum {
    MidlInterceptionInfoVersionOne = 1
};

enum {
    MidlWinrtTypeSerializationInfoVersionOne = 1
};

#define MIDL_WINRT_TYPE_SERIALIZATION_INFO_CURRENT_VERSION 1

typedef unsigned short *PARAM_OFFSETTABLE;

typedef void *PMIDL_XMIT_TYPE;

typedef unsigned short *PPARAM_OFFSETTABLE;

typedef void *RPC_CLIENT_ALLOC(size_t);

typedef void RPC_CLIENT_FREE(void *);

typedef void *RPC_SS_THREAD_HANDLE;

#ifdef __cplusplus
extern "C" {
#endif

void WINAPI NdrComplexArrayBufferSize(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
void WINAPI NdrComplexArrayFree(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrComplexArrayMarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned long WINAPI NdrComplexArrayMemorySize(PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrComplexArrayUnmarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char **ppMemory, PFORMAT_STRING pFormat, unsigned char fMustAlloc);
void WINAPI NdrComplexStructBufferSize(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
void WINAPI NdrComplexStructFree(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrComplexStructMarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned long WINAPI NdrComplexStructMemorySize(PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrComplexStructUnmarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char **ppMemory, PFORMAT_STRING pFormat, unsigned char fMustAlloc);
void WINAPI NdrConformantArrayBufferSize(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
void WINAPI NdrConformantArrayFree(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrConformantArrayMarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned long WINAPI NdrConformantArrayMemorySize(PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrConformantArrayUnmarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char **ppMemory, PFORMAT_STRING pFormat, unsigned char fMustAlloc);
void WINAPI NdrConformantStringBufferSize(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrConformantStringMarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned long WINAPI NdrConformantStringMemorySize(PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrConformantStringUnmarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char **ppMemory, PFORMAT_STRING pFormat, unsigned char fMustAlloc);
void WINAPI NdrConformantStructBufferSize(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
void WINAPI NdrConformantStructFree(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrConformantStructMarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned long WINAPI NdrConformantStructMemorySize(PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrConformantStructUnmarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char **ppMemory, PFORMAT_STRING pFormat, unsigned char fMustAlloc);
void WINAPI NdrConformantVaryingArrayBufferSize(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
void WINAPI NdrConformantVaryingArrayFree(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrConformantVaryingArrayMarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned long WINAPI NdrConformantVaryingArrayMemorySize(PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrConformantVaryingArrayUnmarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char **ppMemory, PFORMAT_STRING pFormat, unsigned char fMustAlloc);
void WINAPI NdrConformantVaryingStructBufferSize(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
void WINAPI NdrConformantVaryingStructFree(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrConformantVaryingStructMarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned long WINAPI NdrConformantVaryingStructMemorySize(PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrConformantVaryingStructUnmarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char **ppMemory, PFORMAT_STRING pFormat, unsigned char fMustAlloc);
void WINAPI NdrEncapsulatedUnionBufferSize(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
void WINAPI NdrEncapsulatedUnionFree(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrEncapsulatedUnionMarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned long WINAPI NdrEncapsulatedUnionMemorySize(PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrEncapsulatedUnionUnmarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char **ppMemory, PFORMAT_STRING pFormat, unsigned char fMustAlloc);
void WINAPI NdrFixedArrayBufferSize(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
void WINAPI NdrFixedArrayFree(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrFixedArrayMarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned long WINAPI NdrFixedArrayMemorySize(PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrFixedArrayUnmarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char **ppMemory, PFORMAT_STRING pFormat, unsigned char fMustAlloc);
void WINAPI NdrInterfacePointerBufferSize(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
void WINAPI NdrInterfacePointerFree(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrInterfacePointerMarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned long WINAPI NdrInterfacePointerMemorySize(PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrInterfacePointerUnmarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char **ppMemory, PFORMAT_STRING pFormat, unsigned char fMustAlloc);
void WINAPI NdrNonConformantStringBufferSize(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrNonConformantStringMarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned long WINAPI NdrNonConformantStringMemorySize(PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrNonConformantStringUnmarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char **ppMemory, PFORMAT_STRING pFormat, unsigned char fMustAlloc);
void WINAPI NdrNonEncapsulatedUnionBufferSize(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
void WINAPI NdrNonEncapsulatedUnionFree(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrNonEncapsulatedUnionMarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned long WINAPI NdrNonEncapsulatedUnionMemorySize(PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrNonEncapsulatedUnionUnmarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char **ppMemory, PFORMAT_STRING pFormat, unsigned char fMustAlloc);
void WINAPI NdrPointerBufferSize(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
void WINAPI NdrPointerFree(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrPointerMarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned long WINAPI NdrPointerMemorySize(PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrPointerUnmarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char **ppMemory, PFORMAT_STRING pFormat, unsigned char fMustAlloc);
void WINAPI NdrSimpleStructBufferSize(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
void WINAPI NdrSimpleStructFree(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrSimpleStructMarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned long WINAPI NdrSimpleStructMemorySize(PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrSimpleStructUnmarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char **ppMemory, PFORMAT_STRING pFormat, unsigned char fMustAlloc);
void WINAPI NdrUserMarshalBufferSize(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
void WINAPI NdrUserMarshalFree(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrUserMarshalMarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned long WINAPI NdrUserMarshalMemorySize(PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrUserMarshalUnmarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char **ppMemory, PFORMAT_STRING pFormat, unsigned char fMustAlloc);
void WINAPI NdrVaryingArrayBufferSize(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
void WINAPI NdrVaryingArrayFree(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrVaryingArrayMarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned long WINAPI NdrVaryingArrayMemorySize(PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrVaryingArrayUnmarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char **ppMemory, PFORMAT_STRING pFormat, unsigned char fMustAlloc);
void WINAPI NdrXmitOrRepAsBufferSize(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
void WINAPI NdrXmitOrRepAsFree(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrXmitOrRepAsMarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned long WINAPI NdrXmitOrRepAsMemorySize(PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat);
unsigned char * WINAPI NdrXmitOrRepAsUnmarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char **ppMemory, PFORMAT_STRING pFormat, unsigned char fMustAlloc);

/* TODO: LiberNT does not implement the functions below yet */
void * I_RpcDefaultAllocate(handle_t bh, size_t size, void *(*RealAlloc)(size_t));
void I_RpcDefaultFree(handle_t bh, void *, void (*RealFree)(void *));
void Ndr64AsyncServerCall(PRPC_MESSAGE pRpcMsg);
void Ndr64AsyncServerCall64(PRPC_MESSAGE pRpcMsg);
void Ndr64AsyncServerCallAll(PRPC_MESSAGE pRpcMsg);
long Ndr64AsyncStubCall(struct IRpcStubBuffer *pThis, struct IRpcChannelBuffer *pChannel, PRPC_MESSAGE pRpcMsg, unsigned long *pdwStubPhase);
CLIENT_CALL_RETURN Ndr64DcomAsyncClientCall(MIDL_STUBLESS_PROXY_INFO *pProxyInfo, unsigned long nProcNum, void *pReturnValue, ...);
long Ndr64DcomAsyncStubCall(struct IRpcStubBuffer *pThis, struct IRpcChannelBuffer *pChannel, PRPC_MESSAGE pRpcMsg, unsigned long *pdwStubPhase);
void NDRCContextUnmarshall2(NDR_CCONTEXT *pCContext, RPC_BINDING_HANDLE hBinding, void *pBuff, unsigned long DataRepresentation);
RPC_STATUS WINAPI NdrClientGetSupportedSyntaxes(RPC_CLIENT_INTERFACE *pInf, unsigned long *pCount, MIDL_SYNTAX_INFO **pArr);
unsigned long NdrContextHandleMemorySize(PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat);
RPC_STATUS WINAPI NdrCreateServerInterfaceFromStub(struct IRpcStubBuffer *pStub, RPC_SERVER_INTERFACE *pServerIf);
void NdrCsArrayBufferSize(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
void NdrCsArrayFree(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned char * NdrCsArrayMarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned long NdrCsArrayMemorySize(PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat);
unsigned char * NdrCsArrayUnmarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char **ppMemory, PFORMAT_STRING pFormat, unsigned char fMustAlloc);
void NdrCsTagBufferSize(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned char * NdrCsTagMarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char *pMemory, PFORMAT_STRING pFormat);
unsigned long NdrCsTagMemorySize(PMIDL_STUB_MESSAGE pStubMsg, PFORMAT_STRING pFormat);
unsigned char * NdrCsTagUnmarshall(PMIDL_STUB_MESSAGE pStubMsg, unsigned char **ppMemory, PFORMAT_STRING pFormat, unsigned char fMustAlloc);
void NdrPartialIgnoreClientBufferSize(PMIDL_STUB_MESSAGE pStubMsg, void *pMemory);
void NdrPartialIgnoreClientMarshall(PMIDL_STUB_MESSAGE pStubMsg, void *pMemory);
void NdrPartialIgnoreServerInitialize(PMIDL_STUB_MESSAGE pStubMsg, void **ppMemory, PFORMAT_STRING pFormat);
void NdrPartialIgnoreServerUnmarshall(PMIDL_STUB_MESSAGE pStubMsg, void **ppMemory);
void NdrServerCall3(PRPC_MESSAGE pRpcMsg);
void NdrServerCallNdr64(PRPC_MESSAGE pRpcMsg);
RPC_STATUS WINAPI NdrServerGetSupportedSyntaxes(RPC_SERVER_INTERFACE *pInf, unsigned long *pCount, MIDL_SYNTAX_INFO **pArr, unsigned long *pPreferSyntaxIndex);
void NdrServerInitializePartial(PRPC_MESSAGE pRpcMsg, PMIDL_STUB_MESSAGE pStubMsg, PMIDL_STUB_DESC pStubDescriptor, unsigned long RequestedBufferSize);
long NdrStubCall3(void *pThis, void *pChannel, PRPC_MESSAGE pRpcMsg, unsigned long *pdwStubPhase);
void RpcCsGetTags(RPC_BINDING_HANDLE hBinding, int fServerSide, unsigned long *pulSendingTag, unsigned long *pulDesiredReceivingTag, unsigned long *pulReceivingTag, error_status_t *pStatus);
void * RpcSmAllocate(size_t Size, RPC_STATUS *pStatus);
RPC_STATUS RpcSmClientFree(void *pNodeToFree);
RPC_STATUS RpcSmDisableAllocate(void);
RPC_STATUS RpcSmEnableAllocate(void);
RPC_STATUS RpcSmFree(void *NodeToFree);
RPC_SS_THREAD_HANDLE RpcSmGetThreadHandle(RPC_STATUS *pStatus);
RPC_STATUS RpcSmSetClientAllocFree(RPC_CLIENT_ALLOC *ClientAlloc, RPC_CLIENT_FREE *ClientFree);
RPC_STATUS RpcSmSetThreadHandle(RPC_SS_THREAD_HANDLE Id);
RPC_STATUS RpcSmSwapClientAllocFree(RPC_CLIENT_ALLOC *ClientAlloc, RPC_CLIENT_FREE *ClientFree, RPC_CLIENT_ALLOC **OldClientAlloc, RPC_CLIENT_FREE **OldClientFree);
void * RpcSsAllocate(size_t Size);
void RpcSsDisableAllocate(void);
void RpcSsEnableAllocate(void);
void RpcSsFree(void *NodeToFree);
RPC_SS_THREAD_HANDLE RpcSsGetThreadHandle(void);
void RpcSsSetClientAllocFree(RPC_CLIENT_ALLOC *ClientAlloc, RPC_CLIENT_FREE *ClientFree);
void RpcSsSetThreadHandle(RPC_SS_THREAD_HANDLE Id);
void RpcSsSwapClientAllocFree(RPC_CLIENT_ALLOC *ClientAlloc, RPC_CLIENT_FREE *ClientFree, RPC_CLIENT_ALLOC **OldClientAlloc, RPC_CLIENT_FREE **OldClientFree);
void WINAPI RpcUserFree(handle_t AsyncHandle, void *pBuffer);

#ifdef __cplusplus
}
#endif


enum _USER_MARSHAL_CB_TYPE
{

};


struct _MIDL_INTERCEPTION_INFO
{
    unsigned long Version;
    PFORMAT_STRING ProcString;
    const unsigned short *ProcFormatOffsetTable;
    unsigned long ProcCount;
    PFORMAT_STRING TypeString;
};

struct _MIDL_WINRT_TYPE_SERIALIZATION_INFO
{
    unsigned long Version;
    PFORMAT_STRING TypeFormatString;
    unsigned short FormatStringSize;
    unsigned short TypeOffset;
    PMIDL_STUB_DESC StubDesc;
};

typedef byte cs_byte;

typedef struct _MIDL_INTERCEPTION_INFO MIDL_INTERCEPTION_INFO;

typedef struct _MIDL_WINRT_TYPE_SERIALIZATION_INFO MIDL_WINRT_TYPE_SERIALIZATION_INFO;

typedef struct _MIDL_INTERCEPTION_INFO *PMIDL_INTERCEPTION_INFO;

typedef struct _MIDL_WINRT_TYPE_SERIALIZATION_INFO *PMIDL_WINRT_TYPE_SERIALIZATION_INFO;

#define _midl_addp(p, n) (p += n)
#define _midl_advmp(mp) * (* (unsigned long * *) & mp) ++
#define _midl_fa2(p) (p = (RPC_BUFPTR) ((ULONG_PTR) (p + 1) & ~0x1))
#define _midl_fa4(p) (p = (RPC_BUFPTR) ((ULONG_PTR) (p + 3) & ~0x3))
#define _midl_fa8(p) (p = (RPC_BUFPTR) ((ULONG_PTR) (p + 7) & ~0x7))
#define _midl_ma1(p, cast) * (* (cast * *) & p) ++
#define _midl_ma2(p, cast) * (* (cast * *) & p) ++
#define _midl_ma4(p, cast) * (* (cast * *) & p) ++
#define _midl_ma8(p, cast) * (* (cast * *) & p) ++
#define _midl_marsh_lhs(p, cast) * (* (cast * *) & p) ++
#define _midl_marsh_up(mp, p) * (* (unsigned long * *) & mp) ++ = (unsigned long) p
#define _midl_unma1(p, cast) * ((cast *) p) ++
#define _midl_unma2(p, cast) * ((cast *) p) ++
#define _midl_unma3(p, cast) * ((cast *) p) ++
#define _midl_unma4(p, cast) * ((cast *) p) ++
#define _midl_unmarsh_up(p) (* (* (unsigned long * *) & p) ++)
#define boolean_from_ndr(source, target) { * (target) = * (* (char * *) & (source) -> Buffer) ++; }
#define byte_from_ndr(source, target) { * (target) = * (* (char * *) & (source) -> Buffer) ++; }
#define NdrFieldOffset(s, f) (LONG_PTR) (& (((s *) 0) -> f))
#define NdrFieldPad(s, f, p, t) ((unsigned long) (NdrFieldOffset(s, f) - NdrFieldOffset(s, p)) - sizeof (t))
#define NdrMarshCCtxtHdl(pc, p) (NDRCContextMarshall((NDR_CCONTEXT) pc, p), p + 20)
#define NdrMarshConfStringHdr(p, s, l) (_midl_ma4(p, unsigned long) = s, _midl_ma4(p, unsigned long) = 0, _midl_ma4(p, unsigned long) = l)
#define NdrUnMarshConfStringHdr(p, s, l) ((s = _midl_unma4(p, unsigned long), (_midl_addp(p, 4)), (l = _midl_unma4(p, unsigned long))
#define small_from_ndr(source, target) { * (target) = * (* (char * *) & (source) -> Buffer) ++; }
#define small_from_ndr_temp(source, target, format) { * (target) = * (* (char * *) (source)) ++; }

#endif /*__WINE_RPCNDR_H */
