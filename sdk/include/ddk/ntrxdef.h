#ifndef _RX_NTDEFS_DEFINED_
#define _RX_NTDEFS_DEFINED_

#define INLINE __inline
#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#endif

#ifndef MINIRDR__NAME
#define RxCaptureFcb PFCB __C_Fcb = (PFCB)(RxContext->pFcb)
#define RxCaptureFobx PFOBX __C_Fobx = (PFOBX)(RxContext->pFobx)
#else
#define RxCaptureFcb PMRX_FCB __C_Fcb = (RxContext->pFcb)
#define RxCaptureFobx PMRX_FOBX __C_Fobx = (RxContext->pFobx)
#endif

#define RxCaptureParamBlock PIO_STACK_LOCATION __C_IrpSp = RxContext->CurrentIrpSp
#define RxCaptureFileObject PFILE_OBJECT __C_FileObject = __C_IrpSp-> FileObject

#define capFcb __C_Fcb
#define capFobx __C_Fobx
#define capPARAMS __C_IrpSp
#define capFileObject __C_FileObject

#define RxAllocatePoolWithTag ExAllocatePoolWithTag
#define RxFreePool ExFreePool

#define RxIsResourceOwnershipStateExclusive(Resource) (FlagOn((Resource)->Flag, ResourceOwnedExclusive))

#define RxMdlIsLocked(Mdl) ((Mdl)->MdlFlags & MDL_PAGES_LOCKED)
#define RxMdlSourceIsNonPaged(Mdl) ((Mdl)->MdlFlags & MDL_SOURCE_IS_NONPAGED_POOL)

#define RxGetRequestorProcess(RxContext) IoGetRequestorProcess(RxContext->CurrentIrp)

#define RxAdjustAllocationSizeforCC(Fcb)                                                       \
{                                                                                              \
    if ((Fcb)->Header.FileSize.QuadPart > (Fcb)->Header.AllocationSize.QuadPart)               \
    {                                                                                          \
        PMRX_NET_ROOT NetRoot = (Fcb)->pNetRoot;                                               \
        ULONGLONG ClusterSize = NetRoot->DiskParameters.ClusterSize;                           \
        ULONGLONG FileSize = (Fcb)->Header.FileSize.QuadPart;                                  \
        ASSERT(ClusterSize != 0);                                                              \
        (Fcb)->Header.AllocationSize.QuadPart = (FileSize + ClusterSize) &~ (ClusterSize - 1); \
    }                                                                                          \
    ASSERT ((Fcb)->Header.ValidDataLength.QuadPart <= (Fcb)->Header.FileSize.QuadPart);        \
}

#define RX_DEFINE_FLAGa, c, d) a = ((1 << c) & d),
#define RxAllocateHeaderMdlBuffer, BufferSize, Mdl) { Mdl = RxAllocateMdl(Buffer, BufferSize); if ((Mdl) && (Mdl -> ByteOffset >= TRANSPORT_HEADER_SIZE)) { SetFlag(Mdl -> MdlFlags, MDL_NETWORK_HEADER); } }
#define RxAllocateMdlBuffer, BufferSize) IoAllocateMdl(Buffer, BufferSize, FALSE, FALSE, NULL)
#define RxAllocatePool ExAllocatePool
#define RxAllocatePoolWithTag2 ExAllocatePool2
#define RxBuildHeaderMdlForNonPagedPoolMdl) MmBuildMdlForNonPagedPool(Mdl)
#define RxBuildPartialHeaderMdlSourceMdl, TargetMdl, Va, Len) { IoBuildPartialMdl(SourceMdl, TargetMdl, Va, Len); if (FlagOn(SourceMdl -> MdlFlags, MDL_NETWORK_HEADER) && (TargetMdl -> ByteOffset >= TRANSPORT_HEADER_SIZE)) { SetFlag(TargetMdl -> MdlFlags, MDL_NETWORK_HEADER); } \
}
#define RXCOMMON_ARGUMENTS RxContext
#define RXCOMMON_SIGNATURE 0
#define RxDisownMdlMdl) { NOTHING; }
#define RxGetRequestorProcessIdRXCONTEXT) IoGetRequestorProcessId((RXCONTEXT) -> CurrentIrp)
#define RxInitializeHeaderMdlMdl, Va, Len) { MmInitializeMdl(Mdl, Va, Len); if (Mdl -> ByteOffset >= TRANSPORT_HEADER_SIZE) { Mdl -> MdlFlags |= MDL_NETWORK_HEADER; } }
#define RxMarkContextPendingRXCONTEXT) IoMarkIrpPending((RXCONTEXT) -> CurrentIrp)
#define RxMdlIsHeaderMdl) (FlagOn(Mdl) -> MdlFlags, MDL_NETWORK_HEADER)
#define RxMdlIsOwnedMdl) (TRUE)
#define RxMdlIsPartialMdl) ((Mdl) -> MdlFlags & MDL_PARTIAL)
#define RxMdlIsProtectedMdl) (FALSE)
#define RxProbeAndLockHeaderPagesMdl, Mode, Access, Status) RxProbeAndLockPages(Mdl, Mode, Access, Status)
#define RxProbeAndLockPagesMdl, Mode, Access, Status) Status = STATUS_SUCCESS; try { MmProbeAndLockPages((Mdl), (Mode), (Access)); } except(EXCEPTION_EXECUTE_HANDLER) { Status = GetExceptionCode(); }
#define RxProtectMdlFromFreeMdl) { NOTHING; }
#define RxSetCancelRoutineIrp, CancelRoutine) IoSetCancelRoutine(Irp, CancelRoutine);
#define RxSprintf 0
#define RxTakeOwnershipOfMdlMdl) { NOTHING; }
#define RxUnlockHeaderPagesMdl) MmUnlockPages(Mdl)
#define RxUnprotectMdlFromFreeMdl) { NOTHING; }
#define TICKS_PER_MILLESECOND 0x2710
#define TICKS_PER_SECOND 0x989680
#define TRANSPORT_HEADER_SIZE 128

#define RX_DEFINE_FLAGa, c, d) a = ((1 << c) & d),
#define RxAllocateHeaderMdlBuffer, BufferSize, Mdl) { Mdl = RxAllocateMdl(Buffer, BufferSize); if ((Mdl) && (Mdl -> ByteOffset >= TRANSPORT_HEADER_SIZE)) { SetFlag(Mdl -> MdlFlags, MDL_NETWORK_HEADER); } }
#define RxBuildHeaderMdlForNonPagedPoolMdl) MmBuildMdlForNonPagedPool(Mdl)
#define RxBuildPartialHeaderMdlSourceMdl, TargetMdl, Va, Len) { IoBuildPartialMdl(SourceMdl, TargetMdl, Va, Len); if (FlagOn(SourceMdl -> MdlFlags, MDL_NETWORK_HEADER) && (TargetMdl -> ByteOffset >= TRANSPORT_HEADER_SIZE)) { SetFlag(TargetMdl -> MdlFlags, MDL_NETWORK_HEADER); } \
}
#define RxCaptureRequestPacket 0
#define RxDisownMdlMdl) { NOTHING; }
#define RxGetRequestorProcessIdRXCONTEXT) IoGetRequestorProcessId((RXCONTEXT) -> CurrentIrp)
#define RxInitializeHeaderMdlMdl, Va, Len) { MmInitializeMdl(Mdl, Va, Len); if (Mdl -> ByteOffset >= TRANSPORT_HEADER_SIZE) { Mdl -> MdlFlags |= MDL_NETWORK_HEADER; } }
#define RxMarkContextPendingRXCONTEXT) IoMarkIrpPending((RXCONTEXT) -> CurrentIrp)
#define RxMdlIsHeaderMdl) (FlagOn(Mdl) -> MdlFlags, MDL_NETWORK_HEADER)
#define RxMdlIsOwnedMdl) (TRUE)
#define RxMdlIsPartialMdl) ((Mdl) -> MdlFlags & MDL_PARTIAL)
#define RxMdlIsProtectedMdl) (FALSE)
#define RxProbeAndLockHeaderPagesMdl, Mode, Access, Status) RxProbeAndLockPages(Mdl, Mode, Access, Status)
#define RxProtectMdlFromFreeMdl) { NOTHING; }
#define RxSetCancelRoutineIrp, CancelRoutine) IoSetCancelRoutine(Irp, CancelRoutine);
#define RxTakeOwnershipOfMdlMdl) { NOTHING; }
#define RxUnlockHeaderPagesMdl) MmUnlockPages(Mdl)
#define RxUnprotectMdlFromFreeMdl) { NOTHING; }

#define capReqPacket 0
#define RX_DEFINE_FLAGa, c, d) a = ((1 << c) & d),
#define RxAllocateHeaderMdlBuffer, BufferSize, Mdl) { Mdl = RxAllocateMdl(Buffer, BufferSize); if ((Mdl) && (Mdl -> ByteOffset >= TRANSPORT_HEADER_SIZE)) { SetFlag(Mdl -> MdlFlags, MDL_NETWORK_HEADER); } }
#define RxBuildHeaderMdlForNonPagedPoolMdl) MmBuildMdlForNonPagedPool(Mdl)
#define RxBuildPartialHeaderMdlSourceMdl, TargetMdl, Va, Len) { IoBuildPartialMdl(SourceMdl, TargetMdl, Va, Len); if (FlagOn(SourceMdl -> MdlFlags, MDL_NETWORK_HEADER) && (TargetMdl -> ByteOffset >= TRANSPORT_HEADER_SIZE)) { SetFlag(TargetMdl -> MdlFlags, MDL_NETWORK_HEADER); } \
}
#define RxDisownMdlMdl) { NOTHING; }
#define RxGetRequestorProcessIdRXCONTEXT) IoGetRequestorProcessId((RXCONTEXT) -> CurrentIrp)
#define RxInitializeHeaderMdlMdl, Va, Len) { MmInitializeMdl(Mdl, Va, Len); if (Mdl -> ByteOffset >= TRANSPORT_HEADER_SIZE) { Mdl -> MdlFlags |= MDL_NETWORK_HEADER; } }
#define RxMarkContextPendingRXCONTEXT) IoMarkIrpPending((RXCONTEXT) -> CurrentIrp)
#define RxMdlIsHeaderMdl) (FlagOn(Mdl) -> MdlFlags, MDL_NETWORK_HEADER)
#define RxMdlIsOwnedMdl) (TRUE)
#define RxMdlIsPartialMdl) ((Mdl) -> MdlFlags & MDL_PARTIAL)
#define RxMdlIsProtectedMdl) (FALSE)
#define RxProbeAndLockHeaderPagesMdl, Mode, Access, Status) RxProbeAndLockPages(Mdl, Mode, Access, Status)
#define RxProtectMdlFromFreeMdl) { NOTHING; }
#define RxSetCancelRoutineIrp, CancelRoutine) IoSetCancelRoutine(Irp, CancelRoutine);
#define RxTakeOwnershipOfMdlMdl) { NOTHING; }
#define RxUnlockHeaderPagesMdl) MmUnlockPages(Mdl)
#define RxUnprotectMdlFromFreeMdl) { NOTHING; }

#endif
