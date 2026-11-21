#ifndef _RXTYPES_INCL
#define _RXTYPES_INCL

#include "nodetype.h"

#define RxMaximumWorkQueue 3

typedef KSPIN_LOCK  RX_SPIN_LOCK;
typedef PKSPIN_LOCK PRX_SPIN_LOCK;

typedef PVOID * PRX_HANDLE;
typedef PVOID RX_HANDLE;

typedef struct _BINDING_HANDLE {
  RX_HANDLE pTdiEmulationContext;
  RX_HANDLE pTransportInformation;
} RX_BINDING_HANDLE, *PRX_BINDING_HANDLE;

typedef struct _RX_BINDING_CONTEXT {
  PUNICODE_STRING pTransportName;
  ULONG QualityOfService;
  PRX_BINDING_HANDLE pBindHandle;
} RX_BINDING_CONTEXT, *PRX_BINDING_CONTEXT;

#endif
