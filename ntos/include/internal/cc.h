

extern ULONG CcLazyWritePages;
extern ULONG CcLazyWriteIos;
extern ULONG CcMapDataWait;
extern ULONG CcMapDataNoWait;
extern ULONG CcPinReadWait;
extern ULONG CcPinReadNoWait;
extern ULONG CcPinMappedDataCount;
extern ULONG CcDataPages;
extern ULONG CcDataFlushes;

CODE_SEG("INIT")

VOID
NTAPI

VOID
NTAPI

NTSTATUS
    _In_ BOOLEAN Wait,

ULONG
CcUnmapIdleViews(
    _In_ SIZE_T Bytes);

VOID
NTAPI

VOID
NTAPI

VOID
NTAPI

VOID
NTAPI

VOID
