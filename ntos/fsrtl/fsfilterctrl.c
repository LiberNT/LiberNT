/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Letting every filter over a volume speak before the file is taken
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "fsrtlp.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

#define FSRTL_FILTER_TAG 'fRsF'

#define FSRTL_FILTER_INLINE_DEVICES 8

#define FSRTL_FAST_IO_HAS(Fast, Member)                                       \
    (((Fast) != NULL) &&                                                      \
     ((Fast)->SizeOfFastIoDispatch >=                                         \
      FIELD_OFFSET(FAST_IO_DISPATCH, Member) + sizeof(PVOID)) &&              \
     ((Fast)->Member != NULL))

#define FSRTL_CALLBACKS_HAS(Callbacks, Member)                                \
    (((Callbacks)->SizeOfFsFilterCallbacks >=                                 \
      FIELD_OFFSET(FS_FILTER_CALLBACKS, Member) + sizeof(PVOID)) &&           \
     ((Callbacks)->Member != NULL))

typedef struct _FSRTL_FILTER_FRAME
{
    PDEVICE_OBJECT DeviceObject;
    PFS_FILTER_COMPLETION_CALLBACK Completion;
    PVOID CompletionContext;
} FSRTL_FILTER_FRAME, *PFSRTL_FILTER_FRAME;

typedef struct _FSRTL_FILTER_STACK
{
    PDEVICE_OBJECT Base;
    PDEVICE_OBJECT *Devices;
    PFSRTL_FILTER_FRAME Frames;
    ULONG DeviceCount;
    ULONG FrameCount;
    PVOID Allocation;
    PDEVICE_OBJECT InlineDevices[FSRTL_FILTER_INLINE_DEVICES];
    FSRTL_FILTER_FRAME InlineFrames[FSRTL_FILTER_INLINE_DEVICES];
} FSRTL_FILTER_STACK, *PFSRTL_FILTER_STACK;

/* PRIVATE FUNCTIONS **********************************************************/

static
PFS_FILTER_CALLBACKS
FsFilterGetCallbacks(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PEXTENDED_DRIVER_EXTENSION Extension;

    if (DeviceObject->DriverObject == NULL) return NULL;

    Extension = IoGetDrvObjExtension(DeviceObject->DriverObject);
    if (Extension == NULL) return NULL;

    return Extension->FsFilterCallbacks;
}

static
PFS_FILTER_CALLBACK
FsFilterPreCallback(
    _In_ PFS_FILTER_CALLBACKS Callbacks,
    _In_ UCHAR Operation)
{
    switch (Operation)
    {
        case FS_FILTER_ACQUIRE_FOR_SECTION_SYNCHRONIZATION:
            if (FSRTL_CALLBACKS_HAS(Callbacks,
                                    PreAcquireForSectionSynchronization))
            {
                return Callbacks->PreAcquireForSectionSynchronization;
            }
            break;

        case FS_FILTER_RELEASE_FOR_SECTION_SYNCHRONIZATION:
            if (FSRTL_CALLBACKS_HAS(Callbacks,
                                    PreReleaseForSectionSynchronization))
            {
                return Callbacks->PreReleaseForSectionSynchronization;
            }
            break;

        case FS_FILTER_ACQUIRE_FOR_MOD_WRITE:
            if (FSRTL_CALLBACKS_HAS(Callbacks, PreAcquireForModifiedPageWriter))
            {
                return Callbacks->PreAcquireForModifiedPageWriter;
            }
            break;

        case FS_FILTER_RELEASE_FOR_MOD_WRITE:
            if (FSRTL_CALLBACKS_HAS(Callbacks, PreReleaseForModifiedPageWriter))
            {
                return Callbacks->PreReleaseForModifiedPageWriter;
            }
            break;

        case FS_FILTER_ACQUIRE_FOR_CC_FLUSH:
            if (FSRTL_CALLBACKS_HAS(Callbacks, PreAcquireForCcFlush))
            {
                return Callbacks->PreAcquireForCcFlush;
            }
            break;

        case FS_FILTER_RELEASE_FOR_CC_FLUSH:
            if (FSRTL_CALLBACKS_HAS(Callbacks, PreReleaseForCcFlush))
            {
                return Callbacks->PreReleaseForCcFlush;
            }
            break;

        case FS_FILTER_QUERY_OPEN:
            if (FSRTL_CALLBACKS_HAS(Callbacks, PreQueryOpen))
            {
                return Callbacks->PreQueryOpen;
            }
            break;

        default:
            break;
    }

    return NULL;
}

static
PFS_FILTER_COMPLETION_CALLBACK
FsFilterPostCallback(
    _In_ PFS_FILTER_CALLBACKS Callbacks,
    _In_ UCHAR Operation)
{
    switch (Operation)
    {
        case FS_FILTER_ACQUIRE_FOR_SECTION_SYNCHRONIZATION:
            if (FSRTL_CALLBACKS_HAS(Callbacks,
                                    PostAcquireForSectionSynchronization))
            {
                return Callbacks->PostAcquireForSectionSynchronization;
            }
            break;

        case FS_FILTER_RELEASE_FOR_SECTION_SYNCHRONIZATION:
            if (FSRTL_CALLBACKS_HAS(Callbacks,
                                    PostReleaseForSectionSynchronization))
            {
                return Callbacks->PostReleaseForSectionSynchronization;
            }
            break;

        case FS_FILTER_ACQUIRE_FOR_MOD_WRITE:
            if (FSRTL_CALLBACKS_HAS(Callbacks,
                                    PostAcquireForModifiedPageWriter))
            {
                return Callbacks->PostAcquireForModifiedPageWriter;
            }
            break;

        case FS_FILTER_RELEASE_FOR_MOD_WRITE:
            if (FSRTL_CALLBACKS_HAS(Callbacks,
                                    PostReleaseForModifiedPageWriter))
            {
                return Callbacks->PostReleaseForModifiedPageWriter;
            }
            break;

        case FS_FILTER_ACQUIRE_FOR_CC_FLUSH:
            if (FSRTL_CALLBACKS_HAS(Callbacks, PostAcquireForCcFlush))
            {
                return Callbacks->PostAcquireForCcFlush;
            }
            break;

        case FS_FILTER_RELEASE_FOR_CC_FLUSH:
            if (FSRTL_CALLBACKS_HAS(Callbacks, PostReleaseForCcFlush))
            {
                return Callbacks->PostReleaseForCcFlush;
            }
            break;

        case FS_FILTER_QUERY_OPEN:
            if (FSRTL_CALLBACKS_HAS(Callbacks, PostQueryOpen))
            {
                return Callbacks->PostQueryOpen;
            }
            break;

        default:
            break;
    }

    return NULL;
}

static
ULONG
FsFilterStackDepth(
    _In_ PDEVICE_OBJECT Top,
    _In_opt_ PDEVICE_OBJECT Base)
{
    PDEVICE_OBJECT Device = Top;
    ULONG Depth = 0;

    while (Device != NULL)
    {
        Depth++;

        if (Device == Base) break;

        Device = IoGetDevObjExtension(Device)->AttachedTo;
    }

    return Depth;
}

static
VOID
FsFilterAllocateCompletionStack(
    _Out_ PFSRTL_FILTER_STACK Stack,
    _In_ PFILE_OBJECT FileObject)
{
    PDEVICE_OBJECT Base = IoGetBaseFileSystemDeviceObject(FileObject);
    PDEVICE_OBJECT Device = IoGetRelatedDeviceObject(FileObject);
    ULONG Depth = FsFilterStackDepth(Device, Base);
    ULONG Index = 0;

    Stack->Allocation = NULL;
    Stack->Base = Base;
    Stack->FrameCount = 0;
    Stack->DeviceCount = 0;
    Stack->Devices = Stack->InlineDevices;
    Stack->Frames = Stack->InlineFrames;

    if (Depth > FSRTL_FILTER_INLINE_DEVICES)
    {
        SIZE_T Size = Depth * (sizeof(PDEVICE_OBJECT) +
                               sizeof(FSRTL_FILTER_FRAME));

        Stack->Allocation = ExAllocatePoolWithTag(NonPagedPool,
                                                  Size,
                                                  FSRTL_FILTER_TAG);

        if (Stack->Allocation != NULL)
        {
            Stack->Devices = (PDEVICE_OBJECT *)Stack->Allocation;
            Stack->Frames = (PFSRTL_FILTER_FRAME)
                (Stack->Devices + Depth);
        }
        else
        {
            /* FIXME: with the pool empty only the lowest filters are told, because the device stack does not fit the inline room */
            Depth = FSRTL_FILTER_INLINE_DEVICES;
        }
    }

    while ((Device != NULL) && (Index < Depth))
    {
        Stack->Devices[Index++] = Device;

        if (Device == Base) break;

        Device = IoGetDevObjExtension(Device)->AttachedTo;
    }

    Stack->DeviceCount = Index;
}

static
VOID
FsFilterFreeCompletionStack(
    _Inout_ PFSRTL_FILTER_STACK Stack)
{
    if (Stack->Allocation != NULL)
    {
        ExFreePoolWithTag(Stack->Allocation, FSRTL_FILTER_TAG);
        Stack->Allocation = NULL;
    }
}

static
NTSTATUS
FsFilterPerformCallbacks(
    _Inout_ PFS_FILTER_CALLBACK_DATA Data,
    _Inout_ PFSRTL_FILTER_STACK Stack)
{
    ULONG Index;

    for (Index = 0; Index < Stack->DeviceCount; Index++)
    {
        PFS_FILTER_CALLBACKS Callbacks;
        PFS_FILTER_CALLBACK Pre;
        PVOID Context = NULL;
        NTSTATUS Status;

        Callbacks = FsFilterGetCallbacks(Stack->Devices[Index]);
        if (Callbacks == NULL) continue;

        Pre = FsFilterPreCallback(Callbacks, Data->Operation);
        if (Pre == NULL) continue;

        Data->DeviceObject = Stack->Devices[Index];
        Status = Pre(Data, &Context);

        if (!NT_SUCCESS(Status)) return Status;

        Stack->Frames[Stack->FrameCount].DeviceObject = Stack->Devices[Index];
        Stack->Frames[Stack->FrameCount].Completion =
            FsFilterPostCallback(Callbacks, Data->Operation);
        Stack->Frames[Stack->FrameCount].CompletionContext = Context;
        Stack->FrameCount++;
    }

    return STATUS_SUCCESS;
}

static
VOID
FsFilterPerformCompletionCallbacks(
    _Inout_ PFS_FILTER_CALLBACK_DATA Data,
    _Inout_ PFSRTL_FILTER_STACK Stack,
    _In_ NTSTATUS Status)
{
    while (Stack->FrameCount != 0)
    {
        PFSRTL_FILTER_FRAME Frame;

        Stack->FrameCount--;
        Frame = &Stack->Frames[Stack->FrameCount];

        if (Frame->Completion == NULL) continue;

        Data->DeviceObject = Frame->DeviceObject;
        Frame->Completion(Data, Status, Frame->CompletionContext);
    }
}

static
PFSRTL_ADVANCED_FCB_HEADER
FsRtlpHeaderOf(
    _In_ PFILE_OBJECT FileObject)
{
    return (PFSRTL_ADVANCED_FCB_HEADER)FileObject->FsContext;
}

static
PFAST_IO_DISPATCH
FsRtlpFastIoOf(
    _In_opt_ PDEVICE_OBJECT DeviceObject)
{
    if ((DeviceObject == NULL) || (DeviceObject->DriverObject == NULL))
    {
        return NULL;
    }

    return DeviceObject->DriverObject->FastIoDispatch;
}

static
NTSTATUS
FsRtlpAcquireForSection(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PDEVICE_OBJECT Base)
{
    PFAST_IO_DISPATCH Fast = FsRtlpFastIoOf(Base);
    PFSRTL_ADVANCED_FCB_HEADER Header;

    if (FSRTL_FAST_IO_HAS(Fast, AcquireFileForNtCreateSection))
    {
        Fast->AcquireFileForNtCreateSection(FileObject);
        return STATUS_SUCCESS;
    }

    Header = FsRtlpHeaderOf(FileObject);
    if (Header == NULL) return STATUS_INVALID_DEVICE_REQUEST;

    FsRtlEnterFileSystem();

    if (Header->Resource != NULL)
    {
        ExAcquireResourceExclusiveLite(Header->Resource, TRUE);
    }

    return STATUS_SUCCESS;
}

static
VOID
FsRtlpReleaseForSection(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PDEVICE_OBJECT Base)
{
    PFAST_IO_DISPATCH Fast = FsRtlpFastIoOf(Base);
    PFSRTL_ADVANCED_FCB_HEADER Header;

    if (FSRTL_FAST_IO_HAS(Fast, ReleaseFileForNtCreateSection))
    {
        Fast->ReleaseFileForNtCreateSection(FileObject);
        return;
    }

    Header = FsRtlpHeaderOf(FileObject);
    if (Header == NULL) return;

    if (Header->Resource != NULL) ExReleaseResourceLite(Header->Resource);

    FsRtlExitFileSystem();
}

static
NTSTATUS
FsRtlpAcquireForModWrite(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PDEVICE_OBJECT Base,
    _In_ PLARGE_INTEGER EndingOffset,
    _Outptr_result_maybenull_ PERESOURCE *ResourceToRelease)
{
    PFAST_IO_DISPATCH Fast = FsRtlpFastIoOf(Base);
    PFSRTL_ADVANCED_FCB_HEADER Header;
    PERESOURCE Resource;

    *ResourceToRelease = NULL;

    if (FSRTL_FAST_IO_HAS(Fast, AcquireForModWrite))
    {
        return Fast->AcquireForModWrite(FileObject,
                                        EndingOffset,
                                        ResourceToRelease,
                                        Base);
    }

    Header = FsRtlpHeaderOf(FileObject);
    if (Header == NULL) return STATUS_INVALID_DEVICE_REQUEST;

    if (EndingOffset->QuadPart > Header->ValidDataLength.QuadPart)
    {
        Resource = Header->Resource;

        if ((Resource != NULL) &&
            !ExAcquireResourceExclusiveLite(Resource, FALSE))
        {
            return STATUS_CANT_WAIT;
        }
    }
    else
    {
        Resource = Header->PagingIoResource;

        if ((Resource != NULL) && !ExAcquireResourceSharedLite(Resource, FALSE))
        {
            return STATUS_CANT_WAIT;
        }
    }

    *ResourceToRelease = Resource;

    return STATUS_SUCCESS;
}

static
VOID
FsRtlpReleaseForModWrite(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PDEVICE_OBJECT Base,
    _In_ PERESOURCE ResourceToRelease)
{
    PFAST_IO_DISPATCH Fast = FsRtlpFastIoOf(Base);

    if (FSRTL_FAST_IO_HAS(Fast, ReleaseForModWrite))
    {
        Fast->ReleaseForModWrite(FileObject, ResourceToRelease, Base);
        return;
    }

    if (ResourceToRelease != NULL) ExReleaseResourceLite(ResourceToRelease);
}

static
NTSTATUS
FsRtlpAcquireForCcFlush(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PDEVICE_OBJECT Base)
{
    PFAST_IO_DISPATCH Fast = FsRtlpFastIoOf(Base);

    if (IoGetTopLevelIrp() == NULL)
    {
        IoSetTopLevelIrp((PIRP)FSRTL_CACHE_TOP_LEVEL_IRP);
    }

    if (FSRTL_FAST_IO_HAS(Fast, AcquireForCcFlush))
    {
        NTSTATUS Status = Fast->AcquireForCcFlush(FileObject, Base);

        if (!NT_SUCCESS(Status) &&
            (IoGetTopLevelIrp() == (PIRP)FSRTL_CACHE_TOP_LEVEL_IRP))
        {
            IoSetTopLevelIrp(NULL);
        }

        return Status;
    }

    return STATUS_SUCCESS;
}

static
VOID
FsRtlpReleaseForCcFlush(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PDEVICE_OBJECT Base)
{
    PFAST_IO_DISPATCH Fast = FsRtlpFastIoOf(Base);

    if (FSRTL_FAST_IO_HAS(Fast, ReleaseForCcFlush))
    {
        Fast->ReleaseForCcFlush(FileObject, Base);
    }

    if (IoGetTopLevelIrp() == (PIRP)FSRTL_CACHE_TOP_LEVEL_IRP)
    {
        IoSetTopLevelIrp(NULL);
    }
}

static
NTSTATUS
FsRtlAcquireFileExclusiveCommon(
    _In_ PFILE_OBJECT FileObject,
    _In_ FS_FILTER_SECTION_SYNC_TYPE SyncType,
    _In_ ULONG PageProtection)
{
    FS_FILTER_CALLBACK_DATA Data;
    FSRTL_FILTER_STACK Stack;
    NTSTATUS Status;

    FsFilterAllocateCompletionStack(&Stack, FileObject);

    RtlZeroMemory(&Data, sizeof(Data));
    Data.SizeOfFsFilterCallbackData = sizeof(Data);
    Data.Operation = FS_FILTER_ACQUIRE_FOR_SECTION_SYNCHRONIZATION;
    Data.FileObject = FileObject;
    Data.Parameters.AcquireForSectionSynchronization.SyncType = SyncType;
    Data.Parameters.AcquireForSectionSynchronization.PageProtection =
        PageProtection;

    Status = FsFilterPerformCallbacks(&Data, &Stack);

    if (NT_SUCCESS(Status))
    {
        Status = FsRtlpAcquireForSection(FileObject,
                                         Stack.Base);
    }

    FsFilterPerformCompletionCallbacks(&Data, &Stack, Status);
    FsFilterFreeCompletionStack(&Stack);

    return Status;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
FsRtlRegisterFileSystemFilterCallbacks(
    _In_ PDRIVER_OBJECT FilterDriverObject,
    _In_ PFS_FILTER_CALLBACKS Callbacks)
{
    PEXTENDED_DRIVER_EXTENSION Extension;
    PFS_FILTER_CALLBACKS Copy;
    PFS_FILTER_CALLBACKS Previous;

    PAGED_CODE();

    if ((FilterDriverObject == NULL) || (Callbacks == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Callbacks->SizeOfFsFilterCallbacks < sizeof(ULONG) * 2)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Copy = ExAllocatePoolWithTag(NonPagedPool,
                                 Callbacks->SizeOfFsFilterCallbacks,
                                 FSRTL_FILTER_TAG);

    if (Copy == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Copy, Callbacks, Callbacks->SizeOfFsFilterCallbacks);

    Extension = IoGetDrvObjExtension(FilterDriverObject);

    if (Extension == NULL)
    {
        ExFreePoolWithTag(Copy, FSRTL_FILTER_TAG);
        return STATUS_INVALID_PARAMETER;
    }

    Previous = InterlockedExchangePointer((PVOID *)&Extension->FsFilterCallbacks,
                                          Copy);

    if (Previous != NULL) ExFreePoolWithTag(Previous, FSRTL_FILTER_TAG);

    return STATUS_SUCCESS;
}

VOID
NTAPI
FsRtlAcquireFileExclusive(
    _In_ PFILE_OBJECT FileObject)
{
    PAGED_CODE();

    FsRtlAcquireFileExclusiveCommon(FileObject, SyncTypeOther, 0);
}

NTSTATUS
NTAPI
FsRtlAcquireToCreateMappedSection(
    _In_ PFILE_OBJECT FileObject,
    _In_ ULONG SectionPageProtection)
{
    PAGED_CODE();

    return FsRtlAcquireFileExclusiveCommon(FileObject,
                                           SyncTypeCreateSection,
                                           SectionPageProtection);
}

VOID
NTAPI
FsRtlReleaseFile(
    _In_ PFILE_OBJECT FileObject)
{
    FS_FILTER_CALLBACK_DATA Data;
    FSRTL_FILTER_STACK Stack;

    FsFilterAllocateCompletionStack(&Stack, FileObject);

    RtlZeroMemory(&Data, sizeof(Data));
    Data.SizeOfFsFilterCallbackData = sizeof(Data);
    Data.Operation = FS_FILTER_RELEASE_FOR_SECTION_SYNCHRONIZATION;
    Data.FileObject = FileObject;

    FsFilterPerformCallbacks(&Data, &Stack);

    FsRtlpReleaseForSection(FileObject, Stack.Base);

    FsFilterPerformCompletionCallbacks(&Data, &Stack, STATUS_SUCCESS);
    FsFilterFreeCompletionStack(&Stack);
}

NTSTATUS
NTAPI
FsRtlAcquireFileForModWriteEx(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER EndingOffset,
    _Outptr_result_maybenull_ PERESOURCE *ResourceToRelease)
{
    FS_FILTER_CALLBACK_DATA Data;
    FSRTL_FILTER_STACK Stack;
    NTSTATUS Status;

    *ResourceToRelease = NULL;

    FsFilterAllocateCompletionStack(&Stack, FileObject);

    RtlZeroMemory(&Data, sizeof(Data));
    Data.SizeOfFsFilterCallbackData = sizeof(Data);
    Data.Operation = FS_FILTER_ACQUIRE_FOR_MOD_WRITE;
    Data.FileObject = FileObject;
    Data.Parameters.AcquireForModifiedPageWriter.EndingOffset = EndingOffset;
    Data.Parameters.AcquireForModifiedPageWriter.ResourceToRelease =
        ResourceToRelease;

    Status = FsFilterPerformCallbacks(&Data, &Stack);

    if (NT_SUCCESS(Status))
    {
        Status = FsRtlpAcquireForModWrite(FileObject,
                                          Stack.Base,
                                          EndingOffset,
                                          ResourceToRelease);
    }

    FsFilterPerformCompletionCallbacks(&Data, &Stack, Status);
    FsFilterFreeCompletionStack(&Stack);

    return Status;
}

VOID
NTAPI
FsRtlReleaseFileForModWrite(
    _In_ PFILE_OBJECT FileObject,
    _In_ PERESOURCE ResourceToRelease)
{
    FS_FILTER_CALLBACK_DATA Data;
    FSRTL_FILTER_STACK Stack;

    FsFilterAllocateCompletionStack(&Stack, FileObject);

    RtlZeroMemory(&Data, sizeof(Data));
    Data.SizeOfFsFilterCallbackData = sizeof(Data);
    Data.Operation = FS_FILTER_RELEASE_FOR_MOD_WRITE;
    Data.FileObject = FileObject;
    Data.Parameters.ReleaseForModifiedPageWriter.ResourceToRelease =
        ResourceToRelease;

    FsFilterPerformCallbacks(&Data, &Stack);

    FsRtlpReleaseForModWrite(FileObject, Stack.Base, ResourceToRelease);

    FsFilterPerformCompletionCallbacks(&Data, &Stack, STATUS_SUCCESS);
    FsFilterFreeCompletionStack(&Stack);
}

NTSTATUS
NTAPI
FsRtlAcquireFileForCcFlushEx(
    _In_ PFILE_OBJECT FileObject)
{
    FS_FILTER_CALLBACK_DATA Data;
    FSRTL_FILTER_STACK Stack;
    NTSTATUS Status;

    FsFilterAllocateCompletionStack(&Stack, FileObject);

    RtlZeroMemory(&Data, sizeof(Data));
    Data.SizeOfFsFilterCallbackData = sizeof(Data);
    Data.Operation = FS_FILTER_ACQUIRE_FOR_CC_FLUSH;
    Data.FileObject = FileObject;

    Status = FsFilterPerformCallbacks(&Data, &Stack);

    if (NT_SUCCESS(Status))
    {
        Status = FsRtlpAcquireForCcFlush(FileObject, Stack.Base);
    }

    FsFilterPerformCompletionCallbacks(&Data, &Stack, Status);
    FsFilterFreeCompletionStack(&Stack);

    return Status;
}

VOID
NTAPI
FsRtlAcquireFileForCcFlush(
    _In_ PFILE_OBJECT FileObject)
{
    FsRtlAcquireFileForCcFlushEx(FileObject);
}

VOID
NTAPI
FsRtlReleaseFileForCcFlush(
    _In_ PFILE_OBJECT FileObject)
{
    FS_FILTER_CALLBACK_DATA Data;
    FSRTL_FILTER_STACK Stack;

    FsFilterAllocateCompletionStack(&Stack, FileObject);

    RtlZeroMemory(&Data, sizeof(Data));
    Data.SizeOfFsFilterCallbackData = sizeof(Data);
    Data.Operation = FS_FILTER_RELEASE_FOR_CC_FLUSH;
    Data.FileObject = FileObject;

    FsFilterPerformCallbacks(&Data, &Stack);

    FsRtlpReleaseForCcFlush(FileObject, Stack.Base);

    FsFilterPerformCompletionCallbacks(&Data, &Stack, STATUS_SUCCESS);
    FsFilterFreeCompletionStack(&Stack);
}

/* EOF */
