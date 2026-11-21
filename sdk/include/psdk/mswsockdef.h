#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#if(_WIN32_WINNT >= 0x0600)
#ifdef _MSC_VER
#define MSWSOCKDEF_INLINE __inline
#else
#define MSWSOCKDEF_INLINE static __inline
#endif
#endif /* (_WIN32_WINNT>=0x0600) */

#ifndef ASSERT
#define MSWSOCKDEF_ASSERT_UNDEFINED
#define ASSERT(exp) ((VOID) 0)
#endif

#if(_WIN32_WINNT >= 0x0600)

#ifdef _WS2DEF_

extern const UCHAR sockaddr_size[AF_MAX];

MSWSOCKDEF_INLINE
UCHAR
SOCKADDR_SIZE(
  IN ADDRESS_FAMILY af)
{
  return (UCHAR)((af < AF_MAX) ? sockaddr_size[af]
                               : sockaddr_size[AF_UNSPEC]);
}

MSWSOCKDEF_INLINE
SCOPE_LEVEL
ScopeLevel(
  IN SCOPE_ID ScopeId)
{
  return (SCOPE_LEVEL)ScopeId.Level;
}

#endif /* _WS2DEF_ */

#define SIO_SET_COMPATIBILITY_MODE _WSAIOW(IOC_VENDOR,300)

typedef enum _WSA_COMPATIBILITY_BEHAVIOR_ID {
  WsaBehaviorAll = 0,
  WsaBehaviorReceiveBuffering,
  WsaBehaviorAutoTuning
} WSA_COMPATIBILITY_BEHAVIOR_ID, *PWSA_COMPATIBILITY_BEHAVIOR_ID;

typedef struct _WSA_COMPATIBILITY_MODE {
  WSA_COMPATIBILITY_BEHAVIOR_ID BehaviorId;
  ULONG TargetOsVersion;
} WSA_COMPATIBILITY_MODE, *PWSA_COMPATIBILITY_MODE;

#endif /* (_WIN32_WINNT>=0x0600) */

#ifdef MSWSOCKDEF_ASSERT_UNDEFINED
#undef ASSERT
#endif

#ifdef __cplusplus
}
#endif

#ifndef RIO_CORRUPT_CQ
#define RIO_CORRUPT_CQ 0xFFFFFFFF
typedef struct _RIORESULT {
  LONG Status;
  ULONG BytesTransferred;
  ULONGLONG SocketContext;
  ULONGLONG RequestContext;
} RIORESULT, *PRIORESULT;

typedef struct _RIO_CMSG_BUFFER {
  ULONG TotalLength;
} RIO_CMSG_BUFFER, *PRIO_CMSG_BUFFER;

#endif
#ifndef RIO_MAX_CQ_SIZE
#define RIO_MAX_CQ_SIZE 0x8000000
#endif
#ifndef RIO_MSG_COMMIT_ONLY
#define RIO_MSG_COMMIT_ONLY 0x00000008
#endif
#ifndef RIO_MSG_DEFER
#define RIO_MSG_DEFER 0x00000002
#endif
#ifndef RIO_MSG_WAITALL
#define RIO_MSG_WAITALL 0x00000004
#endif
