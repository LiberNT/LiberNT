#ifndef _RDBSSDATA_
#define _RDBSSDATA_

extern RX_DISPATCHER RxDispatcher;
extern RX_WORK_QUEUE_DISPATCHER RxDispatcherWorkQueues;

extern KMUTEX RxSerializationMutex;
#define RxAcquireSerializationMutex() KeWaitForSingleObject(&RxSerializationMutex, Executive, KernelMode, FALSE, NULL)
#define RxReleaseSerializationMutex() KeReleaseMutex(&RxSerializationMutex, FALSE)

extern PRDBSS_DEVICE_OBJECT RxFileSystemDeviceObject;

#if DBG
extern ULONG RxFsdEntryCount;
#endif

extern LIST_ENTRY RxSrvCalldownList;
extern LIST_ENTRY RxActiveContexts;

#ifndef RX_PROCESSOR_ARCHITECTURE_ALPHA
#define RX_PROCESSOR_ARCHITECTURE_ALPHA 2
#endif
#ifndef RX_PROCESSOR_ARCHITECTURE_INTEL
#define RX_PROCESSOR_ARCHITECTURE_INTEL 0
#endif
#ifndef RX_PROCESSOR_ARCHITECTURE_MIPS
#define RX_PROCESSOR_ARCHITECTURE_MIPS 1
#endif
#ifndef RX_PROCESSOR_ARCHITECTURE_PPC
#define RX_PROCESSOR_ARCHITECTURE_PPC 3
#endif
#ifndef RX_PROCESSOR_ARCHITECTURE_UNKNOWN
#define RX_PROCESSOR_ARCHITECTURE_UNKNOWN 0xffff
#endif

#endif
