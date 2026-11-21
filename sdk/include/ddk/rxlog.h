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

#endif
