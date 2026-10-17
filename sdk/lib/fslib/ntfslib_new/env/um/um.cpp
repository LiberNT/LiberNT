/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Usermode glue
 * COPYRIGHT:   Copyright 2026 Carl Bialorucki <carl.bialorucki@reactos.org>
 */

#define WIN32_NO_STATUS
#include <windows.h>
#include <winioctl.h>
#include <ndk/umtypes.h> // NTSTATUS, STATUS_*, UNICODE_STRING
#include <ndk/iofuncs.h>
#include <ndk/kefuncs.h>
#include <ndk/ldrfuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/rtlfuncs.h>
#include <ndk/rtltypes.h>
#include <ndk/ldrtypes.h>
extern "C" {
#include <ndk/umfuncs.h>
}
#include <ntfs_um.h>
#include <debug.h>

/* The heap-backed usermode allocator ignores POOL_TYPE's semantics. */

typedef BOOLEAN (NTAPI *PRtlIsNameInExpression)(
    _In_     PUNICODE_STRING Expression,
    _In_     PUNICODE_STRING Name,
    _In_     BOOLEAN         IgnoreCase,
    _In_opt_ PWCH            UpcaseTable
);

BOOLEAN NtfsIsNameInExpressionFallback(
    _In_     PUNICODE_STRING Expression,
    _In_     PUNICODE_STRING Name,
    _In_     BOOLEAN         IgnoreCase,
    _In_opt_ PWCH            UpcaseTable
);

HANDLE VolumeHandle = NULL;
ULONG SectorSize = 0;
static PRtlIsNameInExpression pRtlIsNameInExpression;
static NTSTATUS (NTAPI *pRtlGenerate8dot3Name)(PCUNICODE_STRING,
                                            BOOLEAN,
                                            PGENERATE_NAME_CONTEXT,
                                            PUNICODE_STRING);

NTSTATUS
NtfsDiskInitializeUm(
    _In_      HANDLE FileHandle,
    _Out_opt_ ULONG* BytesPerSector)
{
    DISK_GEOMETRY DiskGeometry;
    IO_STATUS_BLOCK IoStatus;
    static WCHAR NtdllBuffer[] = L"ntdll.dll";
    UNICODE_STRING NtdllName;
    ANSI_STRING Name;
    PVOID Ntdll = NULL;
    PVOID Routine;
    NTSTATUS Status;
    VolumeHandle = FileHandle;

    if (!VolumeHandle || VolumeHandle == INVALID_HANDLE_VALUE)
        return STATUS_INVALID_PARAMETER;

    Status = NtDeviceIoControlFile(VolumeHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatus,
                                   IOCTL_DISK_GET_DRIVE_GEOMETRY,
                                   NULL,
                                   0,
                                   &DiskGeometry,
                                   sizeof(DiskGeometry));
    if (Status == STATUS_PENDING)
    {
        Status = NtWaitForSingleObject(VolumeHandle, FALSE, NULL);
        if (NT_SUCCESS(Status))
            Status = IoStatus.Status;
    }

    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_DEVICE_REQUEST;

    SectorSize = DiskGeometry.BytesPerSector;

    if (BytesPerSector)
        *BytesPerSector = SectorSize;

    // Check if we have RtlIsNameInExpression() in ntdll. If we do, save the pointer.
    pRtlIsNameInExpression = NULL;
    pRtlGenerate8dot3Name = NULL;
    RtlInitUnicodeString(&NtdllName, NtdllBuffer);
    if (NT_SUCCESS(LdrGetDllHandle(NULL, NULL, &NtdllName, &Ntdll)))
    {
        RtlInitAnsiString(&Name, "RtlIsNameInExpression");
        if (NT_SUCCESS(LdrGetProcedureAddress(Ntdll, &Name, 0, &Routine)))
            pRtlIsNameInExpression = reinterpret_cast<PRtlIsNameInExpression>(Routine);
        RtlInitAnsiString(&Name, "RtlGenerate8dot3Name");
        if (NT_SUCCESS(LdrGetProcedureAddress(Ntdll, &Name, 0, &Routine)))
            pRtlGenerate8dot3Name = reinterpret_cast<decltype(pRtlGenerate8dot3Name)>(Routine);
    }

    return STATUS_SUCCESS;
}

#ifdef __cplusplus
extern "C" {
#endif

void
NtfsInitializeMutex(_Out_ PVOID Mutex)
{
    InterlockedExchange(reinterpret_cast<volatile LONG*>(Mutex), 1);
}

void
NtfsAcquireMutex(_Inout_ PVOID Mutex)
{
    while (InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(Mutex), 0, 1) != 1)
        NtYieldExecution();
}

void
NtfsReleaseMutex(_Inout_ PVOID Mutex)
{
    InterlockedExchange(reinterpret_cast<volatile LONG*>(Mutex), 1);
}

void*
NtfsAllocatePoolWithTag(POOL_TYPE PoolType, size_t Size, ULONG Tag)
{
    UNREFERENCED_PARAMETER(PoolType);
    UNREFERENCED_PARAMETER(Tag);
    return RtlAllocateHeap(RtlGetProcessHeap(), 0, Size);
}

void
NtfsFreePool(void* pObject)
{
    if (pObject)
        RtlFreeHeap(RtlGetProcessHeap(), 0, pObject);
}

NTSTATUS
NtfsGenerate8dot3Name(_In_ PCUNICODE_STRING Name,
                      _Inout_ PGENERATE_NAME_CONTEXT Context,
                      _Inout_ PUNICODE_STRING ShortName)
{
    if (!pRtlGenerate8dot3Name)
        return STATUS_PROCEDURE_NOT_FOUND;
    return pRtlGenerate8dot3Name(Name, FALSE, Context, ShortName);
}

NTSTATUS
NtfsQuerySystemTime(_Out_ PULONGLONG NtfsTime)
{
    LARGE_INTEGER Current;
    NTSTATUS Status;

    if (!NtfsTime)
        return STATUS_INVALID_PARAMETER;
    Status = NtQuerySystemTime(&Current);
    if (NT_SUCCESS(Status))
        *NtfsTime = (ULONGLONG)Current.QuadPart;
    return Status;
}

static NTSTATUS
NtfsTransferVolume(_In_    BOOLEAN Write,
                   _In_    ULONGLONG Offset,
                   _In_    ULONG Length,
                   _Inout_ PUCHAR Buffer)
{
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER ByteOffset;
    NTSTATUS Status;

    ByteOffset.QuadPart = Offset;
    if (Write)
        Status = NtWriteFile(VolumeHandle, NULL, NULL, NULL, &IoStatus, Buffer, Length, &ByteOffset, NULL);
    else
        Status = NtReadFile(VolumeHandle, NULL, NULL, NULL, &IoStatus, Buffer, Length, &ByteOffset, NULL);
    if (Status == STATUS_PENDING)
    {
        Status = NtWaitForSingleObject(VolumeHandle, FALSE, NULL);
        if (NT_SUCCESS(Status))
            Status = IoStatus.Status;
    }

    if (!NT_SUCCESS(Status))
        return Status;

    if (IoStatus.Information != Length)
        return Write ? STATUS_UNSUCCESSFUL : STATUS_END_OF_FILE;
    return STATUS_SUCCESS;
}

static NTSTATUS
NtfsAlignedVolumeIo(_In_    BOOLEAN Write,
                    _In_    ULONGLONG Offset,
                    _In_    ULONG Length,
                    _Inout_ PUCHAR Buffer)
{
    ULONGLONG Start, End;
    ULONG Span;
    PUCHAR Bounce;
    NTSTATUS Status;

    if (SectorSize == 0 || (SectorSize & (SectorSize - 1)) != 0 ||
        ((Offset | Length) & (SectorSize - 1)) == 0)
    {
        return NtfsTransferVolume(Write, Offset, Length, Buffer);
    }

    Start = Offset & ~(ULONGLONG)(SectorSize - 1);
    End = (Offset + Length + SectorSize - 1) & ~(ULONGLONG)(SectorSize - 1);
    if (End - Start > MAXULONG)
        return STATUS_INVALID_PARAMETER;
    Span = (ULONG)(End - Start);

    Bounce = static_cast<PUCHAR>(RtlAllocateHeap(RtlGetProcessHeap(), 0, Span));
    if (!Bounce)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (!Write)
    {
        Status = NtfsTransferVolume(FALSE, Start, Span, Bounce);
        if (NT_SUCCESS(Status))
            RtlCopyMemory(Buffer, Bounce + (Offset - Start), Length);
    }
    else
    {
        Status = STATUS_SUCCESS;
        if (Start != Offset)
            Status = NtfsTransferVolume(FALSE, Start, SectorSize, Bounce);
        if (NT_SUCCESS(Status) && End != Offset + Length && (End - SectorSize != Start || Start == Offset))
            Status = NtfsTransferVolume(FALSE, End - SectorSize, SectorSize, Bounce + Span - SectorSize);
        if (NT_SUCCESS(Status))
        {
            RtlCopyMemory(Bounce + (Offset - Start), Buffer, Length);
            Status = NtfsTransferVolume(TRUE, Start, Span, Bounce);
        }
    }

    RtlFreeHeap(RtlGetProcessHeap(), 0, Bounce);
    return Status;
}

NTSTATUS
NtfsReadVolume(_In_    ULONGLONG Offset,
               _In_    ULONG Length,
               _Inout_ PUCHAR Buffer)
{
    return NtfsAlignedVolumeIo(FALSE, Offset, Length, Buffer);
}

NTSTATUS
NtfsWriteVolume(_In_    ULONGLONG Offset,
                _In_    ULONG Length,
                _Inout_ PUCHAR Buffer)
{
    return NtfsAlignedVolumeIo(TRUE, Offset, Length, Buffer);
}

NTSTATUS
NtfsReadVolumeContext(_In_opt_ void* Context,
                      _In_ ULONG BytesPerSector,
                      _In_ ULONGLONG Offset,
                      _In_ ULONG Length,
                      _Inout_ PUCHAR Buffer)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(BytesPerSector);
    return NtfsReadVolume(Offset, Length, Buffer);
}

NTSTATUS
NtfsFlushVolumeContext(_In_opt_ void* Context)
{
    IO_STATUS_BLOCK IoStatus;

    UNREFERENCED_PARAMETER(Context);
    return NtFlushBuffersFile(VolumeHandle, &IoStatus);
}

NTSTATUS
NtfsWriteVolumeContext(_In_opt_ void* Context,
                       _In_ ULONG BytesPerSector,
                       _In_ ULONGLONG Offset,
                       _In_ ULONG Length,
                       _Inout_ PUCHAR Buffer)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(BytesPerSector);
    return NtfsWriteVolume(Offset, Length, Buffer);
}

BOOLEAN
NtfsIsNameInExpression(_In_     PUNICODE_STRING Expression,
                       _In_     PUNICODE_STRING Name,
                       _In_     BOOLEAN IgnoreCase,
                       _In_opt_ PWCHAR UpcaseTable)
{
    // If we have RtlIsNameInExpression() in ntdll, use that.
    if (pRtlIsNameInExpression)
    {
        return pRtlIsNameInExpression(Expression,
                                      Name,
                                      IgnoreCase,
                                      UpcaseTable);
    }

    // If we don't, use the simpler and portable fallback implementation.
    return NtfsIsNameInExpressionFallback(Expression,
                                          Name,
                                          IgnoreCase,
                                          UpcaseTable);
}

#ifdef __cplusplus
}
#endif
