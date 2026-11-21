#ifndef _RDBSSLOG_INCLUDED_
#define _RDBSSLOG_INCLUDED_

VOID
NTAPI
RxDebugControlCommand(
    _In_ PSTR ControlString);

NTSTATUS
NTAPI
RxInitializeLog(
    VOID);

#ifdef RDBSSLOG

#if DBG
#define RxLog(Args) _RxLog##Args
#define RxLogRetail(Args) _RxLog##Args
#else
#define RxLogRetail(Args) _RxLog##Args
#define RxLog(Args) { }
#endif

#define RxPauseLog() _RxPauseLog()
#define RxResumeLog() _RxResumeLog()

#else

#define RxLog(Args) { ;}
#define RxLogRetail(Args) { ;}
#define RxPauseLog() { ; }
#define RxResumeLog() { ; }

#endif

typedef enum RX_LOGGING_STATE {
    RX_LOG_UNINITIALIZED = 0,
    RX_LOG_ENABLED = 1,
    RX_LOG_DISABLED = 2,
    RX_LOG_ERROR = 3
} RX_LOGGING_STATE;

typedef struct _RX_IRP_LIST_ITEM {
  LIST_ENTRY IrpsList;
  PIRP pIrp;
  PMDL CopyDataBuffer;
  ULONG Completed;
} RX_IRP_LIST_ITEM, *PRX_IRP_LIST_ITEM;

typedef struct RX_LOG_ENTRY_HEADER {
  PCHAR Buffer;
} RX_LOG_ENTRY_HEADER, *PRX_LOG_ENTRY_HEADER;

typedef struct RX_LOG {
  RX_SPIN_LOCK SpinLock;
  RX_LOGGING_STATE State;
  PRX_LOG_ENTRY_HEADER CurrentEntry;
  PRX_LOG_ENTRY_HEADER BaseEntry;
  PRX_LOG_ENTRY_HEADER EntryLimit;
  ULONG LogBufferSizeInEntries;
  ULONG NumberOfEntriesIgnored;
  ULONG NumberOfLogWriteAttempts;
  ULONG NumberOfLogWraps;
} RX_LOG, *PRX_LOG;

#ifdef _WIN64
#define MAX_RX_LOG_ENTRY_SIZE 0x60
#else
#define MAX_RX_LOG_ENTRY_SIZE 0x30
#endif
#define RDBSSLOG_ASYNC_NAME_PREFIX 0
#define RxDbgPrintArgs) DbgPrint ## Args

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: LiberNT does not implement the functions below yet */
void _RxPrintLog(ULONG EntriesToPrint);
void RxUninitializeLog(void);

#ifdef __cplusplus
}
#endif

#define RxDbgPrintArgs) DbgPrint ## Args

#define RxDbgPrintArgs) DbgPrint ## Args

#endif
