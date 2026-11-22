/*++ NDK Version: 0098

Copyright (c) Alex Ionescu.  All rights reserved.

Header Name:

    peb_teb.h

Abstract:

    Definition of PEB/PEB32/PEB64 and TEB/TEB32/TEB64

Author:

    Timo Kreuzer (timo.kreuzer@reactos.org)

--*/

#include <rtltypes.h>

#define PASTE2(x,y)       x##y
#define PASTE(x,y)         PASTE2(x,y)

#ifdef EXPLICIT_32BIT
  #define STRUCT(x) PASTE(x,32)
  #define PTR(x) ULONG
#elif defined(EXPLICIT_64BIT)
  #define STRUCT(x) PASTE(x,64)
  #define PTR(x) ULONG64
#else
  #define STRUCT(x) x
  #define PTR(x) x
#endif

#if (defined(_WIN64) && !defined(EXPLICIT_32BIT)) || defined(EXPLICIT_64BIT)
  #define _STRUCT64
  #define _SELECT3264(x32, x64) (x64)
  #define GDI_HANDLE_BUFFER_SIZE 60
#else
  #undef _STRUCT64
  #define _SELECT3264(x32, x64) (x32)
  #define GDI_HANDLE_BUFFER_SIZE 34
#endif

#if defined(_NTDDK_INCLUDED_) || defined(_NTIFS_)
#define PPEB PPEB_RENAMED
#endif

typedef struct STRUCT(_PEB)
{
    BOOLEAN InheritedAddressSpace;
    BOOLEAN ReadImageFileExecOptions;
    BOOLEAN BeingDebugged;
    union
    {
        BOOLEAN BitField;
        struct
        {
            BOOLEAN ImageUsesLargePages:1;
            BOOLEAN IsProtectedProcess:1;
            BOOLEAN IsImageDynamicallyRelocated:1;
            BOOLEAN SkipPatchingUser32Forwarders:1;
            BOOLEAN IsPackagedProcess:1;
            BOOLEAN IsAppContainer:1;
            BOOLEAN IsProtectedProcessLight:1;
            BOOLEAN IsLongPathAwareProcess:1;
        };
    };
#ifdef _STRUCT64
    UCHAR Padding0[4];
#endif
    PTR(HANDLE) Mutant;
    PTR(PVOID) ImageBaseAddress;
    PTR(PPEB_LDR_DATA) Ldr;
    PTR(struct _RTL_USER_PROCESS_PARAMETERS*) ProcessParameters;
    PTR(PVOID) SubSystemData;
    PTR(PVOID) ProcessHeap;
    PTR(struct _RTL_CRITICAL_SECTION*) FastPebLock;
    PTR(PSLIST_HEADER) AtlThunkSListPtr;
    PTR(PVOID) IFEOKey;
    union
    {
        ULONG CrossProcessFlags;
        struct
        {
            ULONG ProcessInJob:1;
            ULONG ProcessInitializing:1;
            ULONG ProcessUsingVEH:1;
            ULONG ProcessUsingVCH:1;
            ULONG ProcessUsingFTH:1;
            ULONG ProcessPreviouslyThrottled:1;
            ULONG ProcessCurrentlyThrottled:1;
            ULONG ProcessImagesHotPatched:1;
            ULONG ReservedBits0:24;
        };
    };
#ifdef _STRUCT64
    UCHAR Padding1[4];
#endif
    union
    {
        PTR(PVOID) KernelCallbackTable;
        PTR(PVOID) UserSharedInfoPtr;
    };
    ULONG SystemReserved[1];
    ULONG AtlThunkSListPtr32;
    PTR(PVOID) ApiSetMap;
    ULONG TlsExpansionCounter;
#ifdef _STRUCT64
    UCHAR Padding2[4];
#endif
    PTR(PVOID) TlsBitmap;
    ULONG TlsBitmapBits[2];
    PTR(PVOID) ReadOnlySharedMemoryBase;
    PTR(PVOID) SharedData;
    PTR(PVOID*) ReadOnlyStaticServerData;
    PTR(PVOID) AnsiCodePageData;
    PTR(PVOID) OemCodePageData;
    PTR(PVOID) UnicodeCaseTableData;
    ULONG NumberOfProcessors;
    ULONG NtGlobalFlag;
    LARGE_INTEGER CriticalSectionTimeout;
    PTR(ULONG_PTR) HeapSegmentReserve;
    PTR(ULONG_PTR) HeapSegmentCommit;
    PTR(ULONG_PTR) HeapDeCommitTotalFreeThreshold;
    PTR(ULONG_PTR) HeapDeCommitFreeBlockThreshold;
    ULONG NumberOfHeaps;
    ULONG MaximumNumberOfHeaps;
    PTR(PVOID*) ProcessHeaps;
    PTR(PVOID) GdiSharedHandleTable;
    PTR(PVOID) ProcessStarterHelper;
    ULONG GdiDCAttributeList;
#ifdef _STRUCT64
    UCHAR Padding3[4];
#endif
    PTR(struct _RTL_CRITICAL_SECTION*) LoaderLock;
    ULONG OSMajorVersion;
    ULONG OSMinorVersion;
    USHORT OSBuildNumber;
    USHORT OSCSDVersion;
    ULONG OSPlatformId;
    ULONG ImageSubsystem;
    ULONG ImageSubsystemMajorVersion;
    ULONG ImageSubsystemMinorVersion;
#ifdef _STRUCT64
    UCHAR Padding4[4];
#endif
    PTR(ULONG_PTR) ActiveProcessAffinityMask;
    ULONG GdiHandleBuffer[GDI_HANDLE_BUFFER_SIZE];
    PTR(PPOST_PROCESS_INIT_ROUTINE) PostProcessInitRoutine;
    PTR(PVOID) TlsExpansionBitmap;
    ULONG TlsExpansionBitmapBits[32];
    ULONG SessionId;
#ifdef _STRUCT64
    UCHAR Padding5[4];
#endif
    ULARGE_INTEGER AppCompatFlags;     // APPCOMPAT_FLAGS
    ULARGE_INTEGER AppCompatFlagsUser; // APPCOMPAT_USERFLAGS
    PTR(PVOID) pShimData;
    PTR(PVOID) AppCompatInfo;
    STRUCT(UNICODE_STRING) CSDVersion;
    PTR(struct _ACTIVATION_CONTEXT_DATA*) ActivationContextData;
    PTR(struct _ASSEMBLY_STORAGE_MAP*) ProcessAssemblyStorageMap;
    PTR(struct _ACTIVATION_CONTEXT_DATA*) SystemDefaultActivationContextData;
    PTR(struct _ASSEMBLY_STORAGE_MAP*) SystemAssemblyStorageMap;
    PTR(ULONG_PTR) MinimumStackCommit;
    PTR(PVOID) SparePointers[2];
    PTR(PVOID) PatchLoaderData;
    PTR(PVOID) ChpeV2ProcessInfo;
    ULONG AppModelFeatureState;
    ULONG SpareUlongs[2];
    USHORT ActiveCodePage;
    USHORT OemCodePage;
    USHORT UseCaseMapping;
    USHORT UnusedNlsField;
    PTR(PVOID) WerRegistrationData;
    PTR(PVOID) WerShipAssertPtr;
#ifdef _STRUCT64
    PTR(PVOID) EcCodeBitMap;
#else
    PTR(PVOID) Spare;
#endif
    PTR(PVOID) pImageHeaderHash;
    union
    {
        ULONG TracingFlags;
        struct
        {
            ULONG HeapTracingEnabled:1;
            ULONG CritSecTracingEnabled:1;
            ULONG LibLoaderTracingEnabled:1;
            ULONG SpareTracingBits:29;
        };
    };
#ifdef _STRUCT64
    UCHAR Padding6[4];
#endif
    ULONGLONG CsrServerReadOnlySharedMemoryBase;
    PTR(ULONG_PTR) TppWorkerpListLock;
    STRUCT(LIST_ENTRY) TppWorkerpList;
    PTR(PVOID) WaitOnAddressHashTable[128];
    PTR(PVOID) TelemetryCoverageHeader;
    ULONG CloudFileFlags;
    ULONG CloudFileDiagFlags;
    CHAR PlaceholderCompatibilityMode;
    CHAR PlaceholderCompatibilityModeReserved[7];
    PTR(PVOID) LeapSecondData;
    union
    {
        ULONG LeapSecondFlags;
        struct
        {
            ULONG SixtySecondEnabled:1;
            ULONG Reserved:31;
        };
    };
    ULONG NtGlobalFlag2;
    ULONGLONG ExtendedFeatureDisableMask;
} STRUCT(PEB), *STRUCT(PPEB);

#undef PPEB

#ifdef _STRUCT64
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), Mutant) == 0x08);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), Ldr) == 0x18);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), FastPebLock) == 0x038);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), ApiSetMap) == 0x068);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), TlsExpansionCounter) == 0x070);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), NtGlobalFlag) == 0x0BC);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), GdiSharedHandleTable) == 0x0F8);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), LoaderLock) == 0x110);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), ImageSubsystem) == 0x128);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), ActiveProcessAffinityMask) == 0x138);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), PostProcessInitRoutine) == 0x230);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), SessionId) == 0x2C0);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), ChpeV2ProcessInfo) == 0x338);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), UseCaseMapping) == 0x350);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), EcCodeBitMap) == 0x368);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), CsrServerReadOnlySharedMemoryBase) == 0x380);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), WaitOnAddressHashTable) == 0x3A0);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), TelemetryCoverageHeader) == 0x7A0);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), NtGlobalFlag2) == 0x7C4);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), ExtendedFeatureDisableMask) == 0x7C8);
C_ASSERT(sizeof(STRUCT(PEB)) == 0x7D0);
#else
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), Mutant) == 0x04);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), Ldr) == 0x0C);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), FastPebLock) == 0x01C);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), ApiSetMap) == 0x038);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), TlsExpansionCounter) == 0x03C);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), NtGlobalFlag) == 0x068);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), GdiSharedHandleTable) == 0x094);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), LoaderLock) == 0x0A0);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), ImageSubsystem) == 0x0B4);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), ActiveProcessAffinityMask) == 0x0C0);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), PostProcessInitRoutine) == 0x14C);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), SessionId) == 0x1D4);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), ChpeV2ProcessInfo) == 0x218);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), UseCaseMapping) == 0x22C);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), Spare) == 0x238);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), CsrServerReadOnlySharedMemoryBase) == 0x248);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), WaitOnAddressHashTable) == 0x25C);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), TelemetryCoverageHeader) == 0x45C);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), NtGlobalFlag2) == 0x478);
C_ASSERT(FIELD_OFFSET(STRUCT(PEB), ExtendedFeatureDisableMask) == 0x480);
C_ASSERT(sizeof(STRUCT(PEB)) == 0x488);
#endif

#define GDI_BATCH_BUFFER_SIZE 0x136
//
// GDI Batch Descriptor
//
typedef struct STRUCT(_GDI_TEB_BATCH)
{
    ULONG Offset;
    PTR(HANDLE) HDC;
    ULONG Buffer[GDI_BATCH_BUFFER_SIZE];
} STRUCT(GDI_TEB_BATCH), *STRUCT(PGDI_TEB_BATCH);

//
// Thread Environment Block (TEB)
//
typedef struct STRUCT(_TEB)
{
    STRUCT(NT_TIB)         NtTib;
    PTR(PVOID)             EnvironmentPointer;
    STRUCT(CLIENT_ID)      ClientId;
    PTR(PVOID)             ActiveRpcHandle;
    PTR(PVOID)             ThreadLocalStoragePointer;
    PTR(STRUCT(PPEB))      ProcessEnvironmentBlock;
    ULONG                  LastErrorValue;
    ULONG                  CountOfOwnedCriticalSections;
    PTR(PVOID)             CsrClientThread;
    PTR(PVOID)             Win32ThreadInfo;
    ULONG                  User32Reserved[26];
    ULONG                  UserReserved[5];
    PTR(PVOID)             WOW32Reserved;
    LCID                   CurrentLocale;
    ULONG                  FpSoftwareStatusRegister;

    PTR(PVOID)             ReservedForDebuggerInstrumentation[16];
    PTR(PVOID)             SystemReserved1[_SELECT3264(21, 25)];
    PTR(PVOID)             HeapFlsData;
    PTR(ULONG_PTR)         RngState[4];
    CHAR                   PlaceholderCompatibilityMode;
    BOOLEAN                PlaceholderHydrationAlwaysExplicit;
    CHAR                   PlaceholderReserved[10];
    ULONG                  ProxiedProcessId;
    STRUCT(ACTIVATION_CONTEXT_STACK) _ActivationStack;
    UCHAR                  WorkingOnBehalfTicket[8];
    LONG                   ExceptionCode;
#ifdef _STRUCT64
    UCHAR                  Padding0[4];
#endif

    PTR(PACTIVATION_CONTEXT_STACK) ActivationContextStackPointer;

    PTR(ULONG_PTR)         InstrumentationCallbackSp;
    PTR(ULONG_PTR)         InstrumentationCallbackPreviousPc;
    PTR(ULONG_PTR)         InstrumentationCallbackPreviousSp;
  #ifdef _STRUCT64
    ULONG                  TxFsContext;
    UCHAR                  InstrumentationCallbackDisabled;
    UCHAR                  UnalignedLoadStoreExceptions;
    UCHAR                  Padding1[2];
  #else
    UCHAR                  InstrumentationCallbackDisabled;
    UCHAR                  SpareBytes[23];
    ULONG                  TxFsContext;
  #endif

    STRUCT(GDI_TEB_BATCH)  GdiTebBatch;
    STRUCT(CLIENT_ID)      RealClientId;
    PTR(PVOID)             GdiCachedProcessHandle;
    ULONG                  GdiClientPID;
    ULONG                  GdiClientTID;
    PTR(PVOID)             GdiThreadLocalInfo;
    PTR(SIZE_T)            Win32ClientInfo[62];
    PTR(PVOID)             glDispatchTable[233];
    PTR(SIZE_T)            glReserved1[29];
    PTR(PVOID)             glReserved2;
    PTR(PVOID)             glSectionInfo;
    PTR(PVOID)             glSection;
    PTR(PVOID)             glTable;
    PTR(PVOID)             glCurrentRC;
    PTR(PVOID)             glContext;
    ULONG                  LastStatusValue;
#ifdef _STRUCT64
    UCHAR                  Padding2[4];
#endif
    STRUCT(UNICODE_STRING) StaticUnicodeString;
    WCHAR                  StaticUnicodeBuffer[261];
#ifdef _STRUCT64
    UCHAR                  Padding3[6];
#endif
    PTR(PVOID)             DeallocationStack;
    PTR(PVOID)             TlsSlots[64];
    STRUCT(LIST_ENTRY)     TlsLinks;
    PTR(PVOID)             Vdm;
    PTR(PVOID)             ReservedForNtRpc;
    PTR(PVOID)             DbgSsReserved[2];
    ULONG                  HardErrorMode;
#ifdef _STRUCT64
    UCHAR                  Padding4[4];
#endif
    PTR(PVOID)             Instrumentation[13 - sizeof(GUID)/sizeof(PTR(PVOID))];
    GUID                   ActivityId;
    PTR(PVOID)             SubProcessTag;
    PTR(PVOID)             PerflibData;
    PTR(PVOID)             EtwTraceData;
    PTR(PVOID)             WinSockData;
    ULONG                  GdiBatchCount;
    union
    {
        PROCESSOR_NUMBER   CurrentIdealProcessor;
        ULONG32            IdealProcessorValue;
        struct
        {
            UCHAR          ReservedPad0;
            UCHAR          ReservedPad1;
            UCHAR          ReservedPad2;
            UCHAR          IdealProcessor;
        };
    };
    ULONG                  GuaranteedStackBytes;
#ifdef _STRUCT64
    UCHAR                  Padding5[4];
#endif
    PTR(PVOID)             ReservedForPerf;
    PTR(PVOID)             ReservedForOle;
    ULONG                  WaitingOnLoaderLock;
#ifdef _STRUCT64
    UCHAR                  Padding6[4];
#endif
    PTR(PVOID)             SavedPriorityState;
    PTR(ULONG_PTR)         ReservedForCodeCoverage;
    PTR(ULONG_PTR)         ThreadPoolData;
    PTR(PVOID*)            TlsExpansionSlots;
#ifdef _STRUCT64
    PTR(PVOID)             ChpeV2CpuAreaInfo;
    PTR(PVOID)             Unused;
#endif
    ULONG                  MuiGeneration;
    ULONG                  IsImpersonating;
    PTR(PVOID)             NlsCache;
    PTR(PVOID)             pShimData;
    ULONG                  HeapData;
#ifdef _STRUCT64
    UCHAR                  Padding7[4];
#endif
    PTR(HANDLE)            CurrentTransactionHandle;
    PTR(PTEB_ACTIVE_FRAME) ActiveFrame;
    PTR(PVOID) FlsData;

    PTR(PVOID) PreferredLanguages;
    PTR(PVOID) UserPrefLanguages;
    PTR(PVOID) MergedPrefLanguages;
    ULONG MuiImpersonation;
    union
    {
        USHORT CrossTebFlags;
        struct
        {
            USHORT SpareCrossTebBits:16;
        };
    };
    union
    {
        USHORT SameTebFlags;
        struct
        {
            USHORT SafeThunkCall:1;
            USHORT InDebugPrint:1;
            USHORT HasFiberData:1;
            USHORT SkipThreadAttach:1;
            USHORT WerInShipAssertCode:1;
            USHORT RanProcessInit:1;
            USHORT ClonedThread:1;
            USHORT SuppressDebugMsg:1;
            USHORT DisableUserStackWalk:1;
            USHORT RtlExceptionAttached:1;
            USHORT InitialThread:1;
            USHORT SessionAware:1;
            USHORT LoadOwner:1;
            USHORT LoaderWorker:1;
            USHORT SkipLoaderInit:1;
            USHORT SkipFileAPIBrokering:1;
        };
    };
    PTR(PVOID) TxnScopeEnterCallback;
    PTR(PVOID) TxnScopeExitCallback;
    PTR(PVOID) TxnScopeContext;
    ULONG LockCount;

    LONG WowTebOffset;

    PTR(PVOID) ResourceRetValue;

    PTR(PVOID) ReservedForWdf;

    ULONG64 ReservedForCrt;
    GUID EffectiveContainerId;

    ULONG64 LastSleepCounter;
    ULONG SpinCallCount;
#ifdef _STRUCT64
    UCHAR Padding8[4];
#endif
    ULONG64 ExtendedFeatureDisableMask;
    PTR(PVOID) SchedulerSharedDataSlot;
    PTR(PVOID) HeapWalkContext;
    STRUCT(GROUP_AFFINITY) PrimaryGroupAffinity;
    ULONG Rcu[2];

} STRUCT(TEB), *STRUCT(PTEB);

#ifdef _STRUCT64
C_ASSERT(FIELD_OFFSET(STRUCT(TEB), NtTib) == 0x000);
C_ASSERT(FIELD_OFFSET(STRUCT(TEB), EnvironmentPointer) == 0x038);
C_ASSERT(FIELD_OFFSET(STRUCT(TEB), ExceptionCode) == 0x2C0);
C_ASSERT(FIELD_OFFSET(STRUCT(TEB), GdiTebBatch) == 0x2F0);
C_ASSERT(FIELD_OFFSET(STRUCT(TEB), LastStatusValue) == 0x1250);
C_ASSERT(FIELD_OFFSET(STRUCT(TEB), Vdm) == 0x1690);
C_ASSERT(FIELD_OFFSET(STRUCT(TEB), HardErrorMode) == 0x16B0);
C_ASSERT(FIELD_OFFSET(STRUCT(TEB), GdiBatchCount) == 0x1740);
C_ASSERT(FIELD_OFFSET(STRUCT(TEB), IdealProcessor) == 0x1747);
C_ASSERT(FIELD_OFFSET(STRUCT(TEB), WaitingOnLoaderLock) == 0x1760);
C_ASSERT(FIELD_OFFSET(STRUCT(TEB), TlsExpansionSlots) == 0x1780);
C_ASSERT(FIELD_OFFSET(STRUCT(TEB), WaitingOnLoaderLock) == 0x1760);
C_ASSERT(FIELD_OFFSET(STRUCT(TEB), ActiveFrame) == 0x17C0);
#else
C_ASSERT(FIELD_OFFSET(STRUCT(TEB), NtTib) == 0x000);
C_ASSERT(FIELD_OFFSET(STRUCT(TEB), EnvironmentPointer) == 0x01C);
C_ASSERT(FIELD_OFFSET(STRUCT(TEB), ExceptionCode) == 0x1A4);
C_ASSERT(FIELD_OFFSET(STRUCT(TEB), GdiTebBatch) == 0x1D4);
C_ASSERT(FIELD_OFFSET(STRUCT(TEB), RealClientId) == 0x6B4);
C_ASSERT(FIELD_OFFSET(STRUCT(TEB), LastStatusValue) == 0xBF4);
C_ASSERT(FIELD_OFFSET(STRUCT(TEB), Vdm) == 0xF18);
C_ASSERT(FIELD_OFFSET(STRUCT(TEB), GdiBatchCount) == 0xF70);
C_ASSERT(FIELD_OFFSET(STRUCT(TEB), TlsExpansionSlots) == 0xF94);
C_ASSERT(FIELD_OFFSET(STRUCT(TEB), ActiveFrame) == 0xFB0);
#endif

#undef _STRUCT64
#undef _SELECT3264
#undef PTR
#undef STRUCT
#undef PASTE
#undef PASTE2
#undef GDI_HANDLE_BUFFER_SIZE
