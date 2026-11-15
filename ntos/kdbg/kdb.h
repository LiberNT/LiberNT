#pragma once
#include "../kd/kd.h"
#include <symz.h>

typedef CONTEXT KDB_KTRAP_FRAME, *PKDB_KTRAP_FRAME;

typedef enum _KDB_BREAKPOINT_TYPE
{
   KdbBreakPointNone = 0,
   KdbBreakPointSoftware,
   KdbBreakPointHardware,
   KdbBreakPointTemporary
} KDB_BREAKPOINT_TYPE;

typedef enum _KDB_ACCESS_TYPE
{
   KdbAccessRead,
   KdbAccessWrite,
   KdbAccessReadWrite,
   KdbAccessExec
} KDB_ACCESS_TYPE;

typedef struct _KDB_BREAKPOINT
{
   PCHAR                  ConditionExpression;
   PVOID                  Condition;
   BOOLEAN                Internal;     /* Set by a step or a run to an address; gone at the next stop */
   ULONG                  HitCount;     /* Times it was reached with its condition holding */
   ULONG                  IgnoreCount;  /* Hits still to let pass without stopping */
   PCHAR                  Commands;     /* What to run when it stops the machine */
   union {
      KD_BREAKPOINT_TYPE  SavedInstruction;
      struct {
         UCHAR            DebugReg : 2;
         UCHAR            Size : 4;
         KDB_ACCESS_TYPE  AccessType;
      } Hw;
   } Data;
} KDB_BREAKPOINT, *PKDB_BREAKPOINT;

typedef enum _KDB_ENTER_CONDITION
{
   KdbDoNotEnter,
   KdbEnterAlways,
   KdbEnterFromKmode,
   KdbEnterFromUmode
} KDB_ENTER_CONDITION;

typedef enum _KD_CONTINUE_TYPE
{
    kdContinue = 0,
    kdDoNotHandleException,
    kdHandleException
} KD_CONTINUE_TYPE;

extern volatile PCHAR KdbInitFileBuffer;

extern PEPROCESS KdbCurrentProcess;
extern PETHREAD KdbCurrentThread;
extern PETHREAD KdbOriginalThread;
extern EXCEPTION_RECORD64 KdbCurrentExceptionRecord;







#if defined(_M_RISCV64)
BOOLEAN
KdbpRiscvGetNextPc(
   IN PCONTEXT Context,
   OUT PULONG_PTR NextPc,
   OUT PCSTR *Reason);
#endif


KdbpStackSwitchAndCall(


NTSTATUS
NTAPI
KdbInitialize(
    _In_ PKD_DISPATCH_TABLE DispatchTable,
    _In_ ULONG BootPhase);

BOOLEAN
NTAPI
KdbRegisterCliCallback(
    PVOID Callback,
    BOOLEAN Deregister);

NTSTATUS
KdbpCliInit(VOID);

VOID
KdbpCliInterpretInitFile(VOID);

VOID

BOOLEAN



VOID
KdbpPager(
    _In_ PCHAR Buffer,
    _In_ ULONG BufLength);

VOID
KdbpPrint(
    _In_ PSTR Format,
    _In_ ...);

VOID
KdbpPrintUnicodeString(
    _In_ PCUNICODE_STRING String);

VOID
KdbpPrintCountedString(
    _In_ PCUNICODE_STRING String);


BOOLEAN

BOOLEAN

BOOLEAN

BOOLEAN

BOOLEAN

BOOLEAN

BOOLEAN

BOOLEAN

VOID

BOOLEAN

LONG
KdbpGetNextBreakPointNr(

NTSTATUS
KdbpInsertBreakPoint(

BOOLEAN



BOOLEAN
KdbpSetEnterCondition(


BOOLEAN

BOOLEAN


NTSTATUS


VOID

VOID
KdbPrintString(
    _In_ const CSTRING* Output);

USHORT
KdbPromptString(
    _In_ const CSTRING* PromptString,
    _Inout_ PSTRING ResponseString);

VOID
KdbPutsN(
    _In_ PCCH String,
    _In_ USHORT Length);



