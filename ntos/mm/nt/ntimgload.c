/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Loading and unloading system images
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <mm/nt/ntsysimg.h>
#define NDEBUG
#include <debug.h>

#define MI_DEFAULT_GS_COOKIE ((sizeof(ULONG_PTR) == 8) ? (ULONG_PTR)0x00002B992DDFA232ULL : (ULONG_PTR)0xBB40E64EUL)
#define MI_GS_COOKIE_MASK ((sizeof(ULONG_PTR) == 8) ? (ULONG_PTR)0x0000FFFFFFFFFFFFULL : (ULONG_PTR)MAXULONG)

BOOLEAN MmEnforceWriteProtection = TRUE;

static volatile LONG64 MiSessionImageCursor;

static
NTSTATUS
MiAllocateSystemImage(
    _In_ ULONG Pages,
    _Out_ PVOID *ImageBase)
{
    ULONG Attempts = 0;
    NTSTATUS Status;
    ULONG64 Base;

    *ImageBase = NULL;

    do
    {
        Status = MiReserveSystemPtesEx(&MiSystem, Pages, &Base);
    } while (Status == STATUS_NO_MEMORY && NT_SUCCESS(MiWaitForMemory(Status, &Attempts)));
    if (!NT_SUCCESS(Status))
        return Status;

    Attempts = 0;

    do
    {
        Status = MiSystemCommitPages(&MiSystem, Base, Pages, MI_PROT_EXECUTE_READWRITE);
    } while (NT_SUCCESS(MiWaitForMemory(Status, &Attempts)) && Status == STATUS_NO_MEMORY);

    if (!NT_SUCCESS(Status))
    {
        MiReleaseSystemPtes(&MiSystem, Base, Pages);
        return Status;
    }

    *ImageBase = (PVOID)(ULONG_PTR)Base;
    return STATUS_SUCCESS;
}

static
BOOLEAN
MiIsLoadedSystemImage(
    _In_ PVOID ImageBase)
{
    PMI_SYSTEM_PTES Ptes = MiSystem.SystemPtes;

    return (BOOLEAN)(Ptes != NULL && (ULONG64)(ULONG_PTR)ImageBase >= Ptes->Base &&
                     (ULONG64)(ULONG_PTR)ImageBase < Ptes->Base + (Ptes->PageCount << PAGE_SHIFT));
}

static
VOID
MiFreeSystemImage(
    _In_ PVOID ImageBase,
    _In_ ULONG Pages)
{
    if (!MiIsLoadedSystemImage(ImageBase))
        return;

    MiSystemDecommitPages(&MiSystem, (ULONG64)(ULONG_PTR)ImageBase, Pages);
    MiReleaseSystemPtes(&MiSystem, (ULONG64)(ULONG_PTR)ImageBase, Pages);
}

static
NTSTATUS
MiSessionImageReserve(
    _In_ ULONG Pages,
    _Out_ PULONG64 Base)
{
    ULONG64 Bytes = (((ULONG64)Pages << PAGE_SHIFT) + MI_ALLOCATION_GRANULARITY - 1) &
                    ~(MI_ALLOCATION_GRANULARITY - 1);
    ULONG64 Limit;
    ULONG64 Offset;

    *Base = 0;

    if (MiSystem.SessionEnd <= MiSystem.SessionStart || Pages == 0)
        return STATUS_NOT_SUPPORTED;

    Limit = (MiSystem.SessionEnd - MiSystem.SessionStart + 1) / 4;
    Offset = (ULONG64)InterlockedExchangeAdd64(&MiSessionImageCursor, (LONG64)Bytes);
    if (Offset + Bytes > Limit || Offset + Bytes < Offset)
        return STATUS_NO_MEMORY;

    /* TODO: the range of an image no session holds any more is never handed out again */
    *Base = MiSystem.SessionStart + Offset;

    ASSERT((*Base & (MI_ALLOCATION_GRANULARITY - 1)) == 0);
    ASSERT(MmIsSessionAddress((PVOID)(ULONG_PTR)*Base));
    ASSERT(MmIsSessionAddress((PVOID)(ULONG_PTR)(*Base + Bytes - 1)));
    return STATUS_SUCCESS;
}

static
NTSTATUS
MiSessionImageCommit(
    _In_ ULONG64 Base,
    _In_ ULONG Pages)
{
    PMI_ADDRESS_SPACE Space = MiSessionSpaceOfProcess(PsGetCurrentProcess());
    ULONG64 Address = Base;
    ULONG64 Size = (ULONG64)Pages << PAGE_SHIFT;
    NTSTATUS Status;

    ASSERT(Pages != 0 && (Base & (PAGE_SIZE - 1)) == 0);
    ASSERT(MmIsSessionAddress((PVOID)(ULONG_PTR)Base));

    if (Space == NULL)
        return STATUS_NOT_SUPPORTED;

    ASSERT(Space->IsSession && Space->IsSystem);
    ASSERT(Base >= Space->LowestVa && Base + Size - 1 <= Space->HighestVa);

    Status = MiAllocateVirtualMemory(Space, &Address, &Size, MI_MEM_RESERVE | MI_MEM_COMMIT,
                                     MI_PROT_EXECUTE_READWRITE);
    ASSERT(!NT_SUCCESS(Status) || (Address == Base && Size >= ((ULONG64)Pages << PAGE_SHIFT)));
    return Status;
}

static
VOID
MiSessionImageRelease(
    _In_ PVOID ImageBase)
{
    PMI_ADDRESS_SPACE Space = MiSessionSpaceOfProcess(PsGetCurrentProcess());
    ULONG64 Address = (ULONG64)(ULONG_PTR)ImageBase;
    ULONG64 Size = 0;
    NTSTATUS Status;

    ASSERT(MmIsSessionAddress(ImageBase));
    ASSERT(Space != NULL && Space->IsSession);

    if (Space == NULL)
        return;

    Status = MiFreeVirtualMemory(Space, &Address, &Size, MI_MEM_RELEASE);
    ASSERT(NT_SUCCESS(Status));
}

static
VOID
MiDiscardImageCopy(
    _In_ PVOID ImageBase,
    _In_ ULONG Pages,
    _In_ BOOLEAN SessionLoad)
{
    if (SessionLoad)
        MiSessionImageRelease(ImageBase);
    else
        MiFreeSystemImage(ImageBase, Pages);
}

static
NTSTATUS
MiLoadImageSection(_Inout_ PSECTION *SectionPtr,
                   _Out_ PVOID *ImageBase,
                   _In_ PUNICODE_STRING FileName,
                   _In_ BOOLEAN SessionLoad,
                   _In_ PLDR_DATA_TABLE_ENTRY LdrEntry)
{
    SECTION_IMAGE_INFORMATION ImageInformation;
    PSECTION Section = *SectionPtr;
    PVOID View = NULL;
    SIZE_T ViewSize = 0;
    NTSTATUS Status;
    ULONG64 SessionBase;
    ULONG Pages;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(FileName);

    ASSERT(Section != NULL);
    ASSERT(SessionLoad || LdrEntry == NULL);
    ASSERT(!SessionLoad || PsGetCurrentProcess()->ProcessInSession);

    *ImageBase = NULL;

    Status = MmGetSectionImageInformation(Section, &ImageInformation);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = MmMapViewInSystemSpace(Section, &View, &ViewSize);
    if (Status == STATUS_IMAGE_MACHINE_TYPE_MISMATCH)
        Status = STATUS_INVALID_IMAGE_FORMAT;

    if (!NT_SUCCESS(Status))
        return Status;

    Pages = (ULONG)(ROUND_TO_PAGES(ViewSize) >> PAGE_SHIFT);

    if (!SessionLoad)
    {
        Status = MiAllocateSystemImage(Pages, ImageBase);
    }
    else
    {
        if (LdrEntry != NULL)
        {
            ASSERT(MmIsSessionAddress(LdrEntry->DllBase));
            ASSERT((ULONG)(ROUND_TO_PAGES(LdrEntry->SizeOfImage) >> PAGE_SHIFT) == Pages);
            SessionBase = (ULONG64)(ULONG_PTR)LdrEntry->DllBase;
            Status = STATUS_SUCCESS;
        }
        else
        {
            Status = MiSessionImageReserve(Pages, &SessionBase);
        }

        if (NT_SUCCESS(Status))
            Status = MiSessionImageCommit(SessionBase, Pages);

        if (NT_SUCCESS(Status))
            *ImageBase = (PVOID)(ULONG_PTR)SessionBase;
    }

    if (NT_SUCCESS(Status))
    {
        ASSERT(*ImageBase != NULL);
        ASSERT(SessionLoad == MmIsSessionAddress(*ImageBase));
        RtlCopyMemory(*ImageBase, View, ViewSize);
        KeSweepICache(*ImageBase, ViewSize);
    }

    MmUnmapViewInSystemSpace(View);
    return Status;
}

static
VOID
MiWriteProtectSystemImage(
    _In_ PVOID ImageBase)
{
    PIMAGE_SECTION_HEADER Section;
    PIMAGE_NT_HEADERS NtHeaders;
    PUCHAR PageProtection;
    ULONG Pages, Page, i;
    PMI_ADDRESS_SPACE SessionSpace = NULL;

    if (!MmEnforceWriteProtection)
        return;

    if (MmIsSessionAddress(ImageBase))
    {
        SessionSpace = MiSessionSpaceOfProcess(PsGetCurrentProcess());
        ASSERT(SessionSpace != NULL && SessionSpace->IsSession);
        if (SessionSpace == NULL)
            return;
    }
    else if (!MiIsLoadedSystemImage(ImageBase))
    {
        return;
    }

    NtHeaders = RtlImageNtHeader(ImageBase);
    if (NtHeaders == NULL)
        return;

    if (NtHeaders->OptionalHeader.MajorOperatingSystemVersion < 5 ||
        NtHeaders->OptionalHeader.MajorSubsystemVersion < 5 ||
        NtHeaders->OptionalHeader.SectionAlignment < PAGE_SIZE)
    {
        return;
    }

    Pages = (ULONG)(ROUND_TO_PAGES(NtHeaders->OptionalHeader.SizeOfImage) >> PAGE_SHIFT);
    PageProtection = ExAllocatePoolWithTag(NonPagedPool, Pages, 'pImM');
    if (PageProtection == NULL)
        return;

    RtlZeroMemory(PageProtection, Pages);
    Section = IMAGE_FIRST_SECTION(NtHeaders);

    for (i = 0; i < NtHeaders->FileHeader.NumberOfSections; i++, Section++)
    {
        ULONG Size = max(Section->SizeOfRawData, Section->Misc.VirtualSize);
        ULONG First = Section->VirtualAddress >> PAGE_SHIFT;
        ULONG Last = (Section->VirtualAddress + Size + PAGE_SIZE - 1) >> PAGE_SHIFT;
        UCHAR Flags = 1;

        if (Section->Characteristics & IMAGE_SCN_MEM_WRITE)
            Flags |= 2;

        if (Section->Characteristics & IMAGE_SCN_MEM_EXECUTE)
            Flags |= 4;

        for (Page = First; Page < Last && Page < Pages; Page++)
            PageProtection[Page] |= Flags;
    }

    for (Page = 0; Page < Pages; )
    {
        ULONG Run = Page;
        UCHAR Flags = PageProtection[Page];
        ULONG Protection;

        while (Run < Pages && PageProtection[Run] == Flags)
            Run++;

        if (Flags & 2)
            Protection = (Flags & 4) ? MI_PROT_EXECUTE_READWRITE : MI_PROT_READWRITE;
        else
            Protection = (Flags & 4) ? MI_PROT_EXECUTE_READ : MI_PROT_READONLY;

        if (SessionSpace != NULL)
        {
            ULONG64 Address = (ULONG64)(ULONG_PTR)ImageBase + ((ULONG64)Page << PAGE_SHIFT);
            ULONG64 Size = (ULONG64)(Run - Page) << PAGE_SHIFT;
            ULONG OldProtection;
            NTSTATUS Status;

            Status = MiProtectVirtualMemory(SessionSpace, &Address, &Size, Protection, &OldProtection);
            ASSERT(NT_SUCCESS(Status));
            UNREFERENCED_PARAMETER(Status);
        }
        else
        {
            MiSystemProtect(&MiSystem, (ULONG64)(ULONG_PTR)ImageBase + ((ULONG64)Page << PAGE_SHIFT),
                            Run - Page, Protection);
        }
        Page = Run;
    }

    ExFreePoolWithTag(PageProtection, 'pImM');
    KeSweepICache(ImageBase, (SIZE_T)Pages << PAGE_SHIFT);
}

static
VOID
MiInitializeImageCookie(
    _In_ PLDR_DATA_TABLE_ENTRY Entry)
{
    PIMAGE_LOAD_CONFIG_DIRECTORY Config;
    ULONG_PTR Offset, Value;
    PULONG_PTR Cookie;
    ULONG Size;

    Config = RtlImageDirectoryEntryToData(Entry->DllBase, TRUE, IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG, &Size);
    if (Config == NULL || Size < RTL_SIZEOF_THROUGH_FIELD(IMAGE_LOAD_CONFIG_DIRECTORY, SecurityCookie))
        return;

    Offset = (ULONG_PTR)Config->SecurityCookie - (ULONG_PTR)Entry->DllBase;
    if (Offset == 0 || Entry->SizeOfImage < sizeof(ULONG_PTR) || Offset > Entry->SizeOfImage - sizeof(ULONG_PTR))
        return;

    Cookie = (PULONG_PTR)Config->SecurityCookie;
    if (*Cookie != MI_DEFAULT_GS_COOKIE && *Cookie != 0)
        return;

    Value = ((ULONG_PTR)KeQueryPerformanceCounter(NULL).QuadPart ^ (ULONG_PTR)KeQueryInterruptTime() ^ (ULONG_PTR)Cookie) &
            MI_GS_COOKIE_MASK;
    if (Value == 0 || Value == MI_DEFAULT_GS_COOKIE)
        Value = MI_DEFAULT_GS_COOKIE ^ 1;

    *Cookie = Value;
}

FORCEINLINE
BOOLEAN
MiImageRunsOnThisSystem(
    _In_ PIMAGE_NT_HEADERS Headers)
{
    return (BOOLEAN)!(KeNumberProcessors > 1 && (Headers->FileHeader.Characteristics & IMAGE_FILE_UP_SYSTEM_ONLY));
}

BOOLEAN
NTAPI
MmVerifyImageIsOkForMpUse(
    _In_ PVOID BaseAddress)
{
    PIMAGE_NT_HEADERS Headers;

    PAGED_CODE();

    Headers = RtlImageNtHeader(BaseAddress);
    return (BOOLEAN)(Headers == NULL || MiImageRunsOnThisSystem(Headers));
}

static
NTSTATUS
MiValidateMappedSystemImage(
    _In_ HANDLE FileHandle,
    _In_ PVOID View,
    _In_ SIZE_T ViewSize)
{
    FILE_STANDARD_INFORMATION Standard;
    IO_STATUS_BLOCK IoStatus;
    PIMAGE_NT_HEADERS Headers;
    NTSTATUS Status;

    Status = ZwQueryInformationFile(FileHandle, &IoStatus, &Standard, sizeof(Standard), FileStandardInformation);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!LdrVerifyMappedImageMatchesChecksum(View, ViewSize, Standard.EndOfFile.LowPart))
        return STATUS_IMAGE_CHECKSUM_MISMATCH;

    Headers = RtlImageNtHeader(View);
    if (Headers == NULL)
        return STATUS_IMAGE_CHECKSUM_MISMATCH;

    if (Headers->FileHeader.Machine != IMAGE_FILE_MACHINE_NATIVE ||
        Headers->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC)
    {
        return STATUS_INVALID_IMAGE_PROTECT;
    }

    return MiImageRunsOnThisSystem(Headers) ? STATUS_SUCCESS : STATUS_IMAGE_MP_UP_MISMATCH;
}

NTSTATUS
NTAPI
MmCheckSystemImage(
    _In_ HANDLE ImageHandle)
{
    OBJECT_ATTRIBUTES Attributes;
    KAPC_STATE ApcState;
    HANDLE SectionHandle;
    PVOID View = NULL;
    SIZE_T ViewSize = 0;
    NTSTATUS Status;

    PAGED_CODE();

    InitializeObjectAttributes(&Attributes, NULL, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateSection(&SectionHandle, SECTION_MAP_EXECUTE, &Attributes, NULL, PAGE_EXECUTE, SEC_IMAGE, ImageHandle);
    if (!NT_SUCCESS(Status))
        return Status;

    KeStackAttachProcess(&PsInitialSystemProcess->Pcb, &ApcState);

    Status = ZwMapViewOfSection(SectionHandle, NtCurrentProcess(), &View, 0, 0, NULL, &ViewSize, ViewShare, 0, PAGE_EXECUTE);
    if (NT_SUCCESS(Status))
    {
        Status = MiValidateMappedSystemImage(ImageHandle, View, ViewSize);
        ZwUnmapViewOfSection(NtCurrentProcess(), View);
    }

    KeUnstackDetachProcess(&ApcState);
    ZwClose(SectionHandle);
    return Status;
}

VOID
NTAPI
MmFreePreparedSystemImage(
    _Inout_ PMM_PREPARED_SYSTEM_IMAGE PreparedImage)
{
    PAGED_CODE();

    if (PreparedImage->Section) ObDereferenceObject(PreparedImage->Section);
    if (PreparedImage->FileHandle) ZwClose(PreparedImage->FileHandle);
    RtlZeroMemory(PreparedImage, sizeof(*PreparedImage));
}

NTSTATUS
NTAPI
MmPrepareSystemImage(
    _In_ PUNICODE_STRING FileName,
    _In_ ULONG Flags,
    _Out_ PMM_PREPARED_SYSTEM_IMAGE PreparedImage)
{
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    HANDLE SectionHandle;
    NTSTATUS Status;

    PAGED_CODE();

    RtlZeroMemory(PreparedImage, sizeof(*PreparedImage));

    if (KdDebuggerEnabled && !KdDebuggerNotPresent)
        KdPullRemoteFile(FileName);

    InitializeObjectAttributes(&Attributes, FileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenFile(&PreparedImage->FileHandle, FILE_EXECUTE, &Attributes, &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_DELETE, 0);
    if (!NT_SUCCESS(Status))
        goto Fail;

    Status = MmCheckSystemImage(PreparedImage->FileHandle);
    if (Status == STATUS_IMAGE_CHECKSUM_MISMATCH || Status == STATUS_IMAGE_MP_UP_MISMATCH ||
        Status == STATUS_INVALID_IMAGE_PROTECT)
    {
        goto Fail;
    }

    InitializeObjectAttributes(&Attributes, NULL, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateSection(&SectionHandle, Flags ? (SECTION_MAP_READ | SECTION_MAP_EXECUTE) : SECTION_ALL_ACCESS,
                             &Attributes, NULL, PAGE_EXECUTE, SEC_IMAGE, PreparedImage->FileHandle);
    if (!NT_SUCCESS(Status))
        goto Fail;

    Status = ObReferenceObjectByHandle(SectionHandle, SECTION_MAP_EXECUTE, MmSectionObjectType, KernelMode,
                                       &PreparedImage->Section, NULL);
    ZwClose(SectionHandle);
    if (NT_SUCCESS(Status))
        return STATUS_SUCCESS;

Fail:
    MmFreePreparedSystemImage(PreparedImage);
    return Status;
}

static
PLDR_DATA_TABLE_ENTRY
MiCreateImageEntry(
    _In_ PVOID ImageBase,
    _In_ ULONG ImageSize,
    _In_ PCUNICODE_STRING BaseName,
    _In_ PCUNICODE_STRING FullName,
    _In_opt_ PSECTION Section)
{
    PIMAGE_NT_HEADERS Headers = RtlImageNtHeader(ImageBase);
    PLDR_DATA_TABLE_ENTRY Entry;

    ASSERT(Headers != NULL);

    Entry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Entry) + BaseName->Length + sizeof(UNICODE_NULL), TAG_MODULE_OBJECT);
    if (Entry == NULL)
        return NULL;

    RtlZeroMemory(Entry, sizeof(*Entry));
    Entry->DllBase = ImageBase;
    Entry->EntryPoint = (PUCHAR)ImageBase + Headers->OptionalHeader.AddressOfEntryPoint;
    Entry->SizeOfImage = ImageSize;
    Entry->CheckSum = Headers->OptionalHeader.CheckSum;
    Entry->SectionPointer = Section;
    Entry->LoadCount = 1;
    Entry->LoadedImports = MM_SYSLDR_NO_IMPORTS;
    Entry->Flags = LDRP_LOAD_IN_PROGRESS;
    if (Headers->OptionalHeader.MajorOperatingSystemVersion >= 5 && Headers->OptionalHeader.MajorImageVersion >= 5)
        Entry->Flags |= LDRP_ENTRY_NATIVE;

    Entry->BaseDllName.Buffer = (PWCH)(Entry + 1);
    Entry->BaseDllName.Length = BaseName->Length;
    Entry->BaseDllName.MaximumLength = BaseName->Length + sizeof(UNICODE_NULL);
    RtlCopyMemory(Entry->BaseDllName.Buffer, BaseName->Buffer, BaseName->Length);
    Entry->BaseDllName.Buffer[BaseName->Length / sizeof(WCHAR)] = UNICODE_NULL;

    Entry->FullDllName.Buffer = ExAllocatePoolWithTag(PagedPool, FullName->Length + sizeof(UNICODE_NULL), TAG_LDR_WSTR);
    if (Entry->FullDllName.Buffer != NULL)
    {
        Entry->FullDllName.Length = FullName->Length;
        Entry->FullDllName.MaximumLength = FullName->Length + sizeof(UNICODE_NULL);
        RtlCopyMemory(Entry->FullDllName.Buffer, FullName->Buffer, FullName->Length);
        Entry->FullDllName.Buffer[FullName->Length / sizeof(WCHAR)] = UNICODE_NULL;
    }

    return Entry;
}

static
VOID
MiFreeImageEntry(
    _In_ PLDR_DATA_TABLE_ENTRY Entry)
{
    ASSERT(Entry->InLoadOrderLinks.Flink == NULL);

    if (Entry->FullDllName.Buffer != NULL)
        ExFreePoolWithTag(Entry->FullDllName.Buffer, TAG_LDR_WSTR);

    if (Entry->SectionPointer != NULL)
        ObDereferenceObject(Entry->SectionPointer);

    ExFreePoolWithTag(Entry, TAG_MODULE_OBJECT);
}

static
VOID
MiAnnounceSystemImage(
    _In_ PLDR_DATA_TABLE_ENTRY Entry,
    _In_ PUNICODE_STRING FileName,
    _In_ PCUNICODE_STRING FullName)
{
    static const UNICODE_STRING SystemRoot = RTL_CONSTANT_STRING(L"\\SystemRoot");
    CHAR Buffer[MAXIMUM_FILENAME_LENGTH];
    UNICODE_STRING Rest;
    IMAGE_INFO ImageInfo;
    STRING SymbolName;
    ULONG Size;

    if (PsImageNotifyEnabled)
    {
        RtlZeroMemory(&ImageInfo, sizeof(ImageInfo));
        ImageInfo.ImageAddressingMode = IMAGE_ADDRESSING_MODE_32BIT;
        ImageInfo.SystemModeImage = TRUE;
        ImageInfo.ImageSize = Entry->SizeOfImage;
        ImageInfo.ImageBase = Entry->DllBase;
        PspRunLoadImageNotifyRoutines(FileName, NULL, &ImageInfo);
    }

#ifndef KDBG
    if (RtlImageDirectoryEntryToData(Entry->DllBase, TRUE, IMAGE_DIRECTORY_ENTRY_DEBUG, &Size) == NULL)
        return;
#else
    UNREFERENCED_PARAMETER(Size);
#endif

    if (FullName->Length > SystemRoot.Length && RtlPrefixUnicodeString(&SystemRoot, FullName, TRUE))
    {
        Rest.Buffer = FullName->Buffer + SystemRoot.Length / sizeof(WCHAR);
        Rest.Length = FullName->Length - SystemRoot.Length;
        Rest.MaximumLength = Rest.Length;
        RtlStringCbPrintfA(Buffer, sizeof(Buffer), "%ws%wZ", &SharedUserData->NtSystemRoot[2], &Rest);
    }
    else
    {
        RtlStringCbPrintfA(Buffer, sizeof(Buffer), "%wZ", &Entry->BaseDllName);
    }

    RtlInitString(&SymbolName, Buffer);
    DbgLoadImageSymbols(&SymbolName, Entry->DllBase, (ULONG_PTR)PsGetCurrentProcessId());
    Entry->Flags |= LDRP_DEBUG_SYMBOLS_LOADED;
}

static
NTSTATUS
MiSessionLoadExistingImage(
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry,
    _In_ PUNICODE_STRING BaseDirectory)
{
    PEPROCESS Process = PsGetCurrentProcess();
    PVOID LoadedImports = MM_SYSLDR_NO_IMPORTS;
    PSECTION Section;
    PVOID ImageBase;
    NTSTATUS Status;

    PAGED_CODE();

    ASSERT(Process->ProcessInSession);
    ASSERT(LdrEntry != NULL && LdrEntry->DllBase != NULL);
    ASSERT(!(LdrEntry->Flags & LDRP_LOAD_IN_PROGRESS));

    if (!MmIsSessionAddress(LdrEntry->DllBase))
        return STATUS_IMAGE_ALREADY_LOADED;

    if (MiSessionHasImage(Process, LdrEntry))
        return STATUS_IMAGE_ALREADY_LOADED;

    Section = LdrEntry->SectionPointer;
    ASSERT(Section != NULL);
    if (Section == NULL)
        return STATUS_INVALID_IMAGE_FORMAT;

    Status = MiSessionAddImage(Process, LdrEntry);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = MiLoadImageSection(&Section, &ImageBase, NULL, TRUE, LdrEntry);
    if (!NT_SUCCESS(Status))
    {
        MiSessionRemoveImage(Process, LdrEntry);
        return Status;
    }

    ASSERT(ImageBase == LdrEntry->DllBase);
    ASSERT(Section == LdrEntry->SectionPointer);

    Status = LdrRelocateImageWithBias(ImageBase,
                                      0,
                                      "SYSLDR",
                                      STATUS_SUCCESS,
                                      STATUS_CONFLICTING_ADDRESSES,
                                      STATUS_INVALID_IMAGE_FORMAT);
    if (NT_SUCCESS(Status))
        Status = MiBindImports(ImageBase, &LdrEntry->BaseDllName, BaseDirectory, &LoadedImports);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("%wZ could not be placed in this session (0x%lx)\n", &LdrEntry->BaseDllName, Status);
        MiSessionImageRelease(ImageBase);
        MiSessionRemoveImage(Process, LdrEntry);
        return Status;
    }

    MiDropImportReferences(LoadedImports);
    MiFreeImportList(LoadedImports);

    MiWriteProtectSystemImage(ImageBase);
    MiInitializeImageCookie(LdrEntry);

    ASSERT(MiSessionHasImage(Process, LdrEntry));
    return STATUS_SUCCESS;
}

static
VOID
MiSplitImagePath(
    _In_ PCUNICODE_STRING Path,
    _Out_ PUNICODE_STRING Directory,
    _Out_ PUNICODE_STRING BaseName)
{
    USHORT Chars = Path->Length / sizeof(WCHAR);
    USHORT Split = 0;

    if (Chars != 0 && Path->Buffer[0] == OBJ_NAME_PATH_SEPARATOR)
    {
        for (Split = Chars; Path->Buffer[Split - 1] != OBJ_NAME_PATH_SEPARATOR; Split--)
            NOTHING;
    }

    Directory->Buffer = Path->Buffer;
    Directory->Length = Directory->MaximumLength = Split * sizeof(WCHAR);
    BaseName->Buffer = Path->Buffer + Split;
    BaseName->Length = BaseName->MaximumLength = (Chars - Split) * sizeof(WCHAR);
}

NTSTATUS
NTAPI
MmLoadSystemImageEx(
    _In_ PUNICODE_STRING FileName,
    _In_opt_ PUNICODE_STRING NamePrefix,
    _In_opt_ PUNICODE_STRING LoadedName,
    _In_ ULONG Flags,
    _Inout_opt_ PMM_PREPARED_SYSTEM_IMAGE PreparedImage,
    _Out_ PVOID *ModuleObject,
    _Out_ PVOID *ImageBaseAddress)
{
    BOOLEAN SessionLoad = (BOOLEAN)(Flags != 0);
    UNICODE_STRING Directory, BaseName, FullName;
    PLDR_DATA_TABLE_ENTRY Entry;
    MM_PREPARED_SYSTEM_IMAGE Prepared = { NULL, NULL };
    PIMAGE_NT_HEADERS Headers;
    PWCHAR PrefixedBuffer = NULL;
    PVOID ImageBase, Imports;
    ULONG ImageSize;
    ULONG Bytes;
    NTSTATUS Status;

    PAGED_CODE();

    if (SessionLoad)
    {
        ASSERT(NamePrefix == NULL && LoadedName == NULL);
        if (!PsGetCurrentProcess()->ProcessInSession)
            return STATUS_NO_MEMORY;
    }

    MiSplitImagePath(FileName, &Directory, &BaseName);
    FullName = *FileName;

    if (NamePrefix != NULL)
    {
        Bytes = (ULONG)Directory.Length + NamePrefix->Length + BaseName.Length;
        if (Bytes > UNICODE_STRING_MAX_BYTES)
            return STATUS_INVALID_PARAMETER;

        PrefixedBuffer = ExAllocatePoolWithTag(PagedPool, Bytes, TAG_LDR_WSTR);
        if (PrefixedBuffer == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        FullName.Buffer = PrefixedBuffer;
        FullName.Length = 0;
        FullName.MaximumLength = (USHORT)Bytes;
        RtlAppendUnicodeStringToString(&FullName, &Directory);
        RtlAppendUnicodeStringToString(&FullName, NamePrefix);
        RtlAppendUnicodeStringToString(&FullName, &BaseName);

        BaseName.Buffer = FullName.Buffer + Directory.Length / sizeof(WCHAR);
        BaseName.Length = FullName.Length - Directory.Length;
        BaseName.MaximumLength = BaseName.Length;
    }

    if (LoadedName != NULL)
        BaseName = *LoadedName;

    if (NtGlobalFlag & FLG_SHOW_LDR_SNAPS)
        DPRINT1("Loading %wZ (%wZ)%s\n", &FullName, &BaseName, SessionLoad ? " in session space" : "");

    for (;;)
    {
        MiLockSystemImages();

        Entry = MiFindSystemImage(&FullName, TRUE);
        if (Entry != NULL || Prepared.Section != NULL)
            break;

        MiUnlockSystemImages();

        if (PreparedImage != NULL && PreparedImage->Section != NULL)
        {
            Prepared = *PreparedImage;
            RtlZeroMemory(PreparedImage, sizeof(*PreparedImage));
        }
        else
        {
            Status = MmPrepareSystemImage(FileName, Flags, &Prepared);
            if (!NT_SUCCESS(Status))
                goto Cleanup;
        }
    }

    if (Entry != NULL)
    {
        Status = SessionLoad ? MiSessionLoadExistingImage(Entry, &Directory) : STATUS_IMAGE_ALREADY_LOADED;
        if (NT_SUCCESS(Status) || Status == STATUS_IMAGE_ALREADY_LOADED)
        {
            *ModuleObject = Entry;
            *ImageBaseAddress = Entry->DllBase;
        }

        goto Unlock;
    }

    Status = MiLoadImageSection((PSECTION *)&Prepared.Section, &ImageBase, FileName, SessionLoad, NULL);
    ASSERT(Status != STATUS_ALREADY_COMMITTED);
    if (!SessionLoad)
    {
        ObDereferenceObject(Prepared.Section);
        Prepared.Section = NULL;
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("%wZ could not be placed in system space (0x%lx)\n", &FullName, Status);
        goto Unlock;
    }

    Headers = RtlImageNtHeader(ImageBase);
    ASSERT(Headers != NULL);
    ImageSize = (ULONG)ROUND_TO_PAGES(Headers->OptionalHeader.SizeOfImage);

    Status = LdrRelocateImageWithBias(ImageBase, 0, "SYSLDR", STATUS_SUCCESS,
                                      STATUS_CONFLICTING_ADDRESSES, STATUS_INVALID_IMAGE_FORMAT);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("%wZ could not be relocated (0x%lx)\n", &FullName, Status);
        MiDiscardImageCopy(ImageBase, ImageSize >> PAGE_SHIFT, SessionLoad);
        goto Unlock;
    }

    Entry = MiCreateImageEntry(ImageBase, ImageSize, &BaseName, &FullName, Prepared.Section);
    if (Entry == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        MiDiscardImageCopy(ImageBase, ImageSize >> PAGE_SHIFT, SessionLoad);
        goto Unlock;
    }

    ASSERT(SessionLoad ? (Prepared.Section != NULL && MmIsSessionAddress(ImageBase)) : (Prepared.Section == NULL));
    Prepared.Section = NULL;

    if (SessionLoad)
    {
        Status = MiSessionAddImage(PsGetCurrentProcess(), Entry);
        if (!NT_SUCCESS(Status))
        {
            MiDiscardImageCopy(ImageBase, ImageSize >> PAGE_SHIFT, SessionLoad);
            MiFreeImageEntry(Entry);
            goto Unlock;
        }
    }

    MiPublishSystemImage(Entry);

    Status = MiBindImports(ImageBase, &Entry->BaseDllName, &Directory, &Imports);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("The imports of %wZ could not be bound (0x%lx)\n", &FullName, Status);
        MiWithdrawSystemImage(Entry);
        if (SessionLoad)
            MiSessionRemoveImage(PsGetCurrentProcess(), Entry);
        MiDiscardImageCopy(ImageBase, ImageSize >> PAGE_SHIFT, SessionLoad);
        MiFreeImageEntry(Entry);
        goto Unlock;
    }

    Entry->LoadedImports = Imports;
    Entry->Flags |= LDRP_SYSTEM_MAPPED | LDRP_ENTRY_PROCESSED | LDRP_MM_LOADED;
    Entry->Flags &= ~LDRP_LOAD_IN_PROGRESS;

    MiWriteProtectSystemImage(ImageBase);
    MiInitializeImageCookie(Entry);
    ASSERT(!SessionLoad || MiSessionHasImage(PsGetCurrentProcess(), Entry));

    MiAnnounceSystemImage(Entry, FileName, &FullName);

    *ModuleObject = Entry;
    *ImageBaseAddress = ImageBase;

Unlock:
    MiUnlockSystemImages();

Cleanup:
    MmFreePreparedSystemImage(&Prepared);
    if (PrefixedBuffer != NULL)
        ExFreePoolWithTag(PrefixedBuffer, TAG_LDR_WSTR);

    return Status;
}

NTSTATUS
NTAPI
MmLoadSystemImage(
    _In_ PUNICODE_STRING FileName,
    _In_opt_ PUNICODE_STRING NamePrefix,
    _In_opt_ PUNICODE_STRING LoadedName,
    _In_ ULONG Flags,
    _Out_ PVOID *ModuleObject,
    _Out_ PVOID *ImageBaseAddress)
{
    return MmLoadSystemImageEx(FileName, NamePrefix, LoadedName, Flags, NULL, ModuleObject, ImageBaseAddress);
}

NTSTATUS
NTAPI
MmUnloadSystemImage(
    _In_ PVOID ImageHandle)
{
    PLDR_DATA_TABLE_ENTRY Entry = ImageHandle;
    BOOLEAN Published;
    ANSI_STRING Name;
    PVOID Imports;

    MiLockSystemImages();

    if (Entry->LoadedImports == MM_SYSLDR_BOOT_LOADED)
        goto Unlock;

    ASSERT(Entry->LoadCount != 0);
    if (--Entry->LoadCount != 0)
        goto Unlock;

    if ((Entry->Flags & LDRP_DEBUG_SYMBOLS_LOADED) &&
        NT_SUCCESS(RtlUnicodeStringToAnsiString(&Name, &Entry->BaseDllName, TRUE)))
    {
        DbgUnLoadImageSymbols(&Name, Entry->DllBase, (ULONG_PTR)PsGetCurrentProcessId());
        RtlFreeAnsiString(&Name);
    }

    Published = (BOOLEAN)(Entry->InLoadOrderLinks.Flink != NULL);
    if (Published)
        MiWithdrawSystemImage(Entry);

    MiFreeSystemImage(Entry->DllBase, (ULONG)(ROUND_TO_PAGES(Entry->SizeOfImage) >> PAGE_SHIFT));

    Imports = Entry->LoadedImports;
    Entry->LoadedImports = MM_SYSLDR_NO_IMPORTS;
    MiDropImportReferences(Imports);
    MiFreeImportList(Imports);

    if (Published)
        MiFreeImageEntry(Entry);

Unlock:
    MiUnlockSystemImages();
    return STATUS_SUCCESS;
}
